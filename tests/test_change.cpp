// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Appearance.h"
#include "app/Change.h"
#include "app/ChangeCard.h"
#include "app/ChangeRunner.h"
#include "app/NeverWritten.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;

namespace {

QByteArray read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(data);
}

} // namespace

// Every change here runs in a folder of the test's own, never on the person's
// settings or files, and none asks for the administrator.
class TestChange : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void shellWordsFollowQuoting()
    {
        QCOMPARE(shellWords(QStringLiteral("a 'b c' \"d \\\"e\\\"\" f\\ g")),
                 (QStringList{QStringLiteral("a"), QStringLiteral("b c"), QStringLiteral("d \"e\""), QStringLiteral("f g")}));
        QCOMPARE(shellWords(QStringLiteral("x ; y")), (QStringList{QStringLiteral("x"), QStringLiteral(";"), QStringLiteral("y")}));
    }

    void aProposalNeedsATitleAndACommand()
    {
        QString why;
        QVERIFY(!ChangeProposal::fromJson("[]", &why));
        QVERIFY(!why.isEmpty());
        QVERIFY(!ChangeProposal::fromJson(R"({"command":"true"})", &why));
        QVERIFY(!ChangeProposal::fromJson(R"({"title":"Nothing"})", &why));
        const auto change = ChangeProposal::fromJson(R"({"title":"  Turn   on  ","command":"true","files":["~/a",""]})");
        QVERIFY(change);
        QCOMPARE(change->title, QStringLiteral("Turn on"));
        QCOMPARE(change->what, QStringLiteral("Turn on"));
        QCOMPARE(change->files, QStringList{QStringLiteral("~/a")});
        QVERIFY(!change->admin);
    }

    // The assistant never brings its own way to the administrator: sudo and
    // the like become Split Rock's own prompt.
    void sudoBecomesTheSystemsPrompt()
    {
        const auto change = ChangeProposal::fromJson(
            R"({"title":"Quiet fans","command":"sudo asusctl profile -P Quiet","undo":"pkexec asusctl profile -P Balanced"})");
        QVERIFY(change);
        QVERIFY(change->admin);
        QCOMPARE(change->command, QStringLiteral("asusctl profile -P Quiet"));
        QCOMPARE(change->undo, QStringLiteral("asusctl profile -P Balanced"));
    }

    void aWholeSystemUpdateIsCaught()
    {
        const auto updater = [](const QString &command) {
            ChangeProposal change;
            change.title = QStringLiteral("Update");
            change.command = command;
            return change.wholeSystemUpdate();
        };
        QCOMPARE(updater(QStringLiteral("pacman -Syu --noconfirm")), QStringLiteral("pacman"));
        QCOMPARE(updater(QStringLiteral("/usr/bin/pacman -S -y -u")), QStringLiteral("pacman"));
        QCOMPARE(updater(QStringLiteral("pacman --sync --refresh --sysupgrade")), QStringLiteral("pacman"));
        QCOMPARE(updater(QStringLiteral("pacman -Su")), QStringLiteral("pacman"));
        QCOMPARE(updater(QStringLiteral("paru")), QStringLiteral("paru"));
        QCOMPARE(updater(QStringLiteral("paru --noconfirm")), QStringLiteral("paru"));
        QCOMPARE(updater(QStringLiteral("yay -Sua")), QStringLiteral("yay"));
        QCOMPARE(updater(QStringLiteral("flatpak update -y && pacman -Syu")), QStringLiteral("pacman"));
        QCOMPARE(updater(QStringLiteral("bash -c 'pacman -Syyu'")), QStringLiteral("pacman"));
        // Fetching the lists without updating leaves the system half updated.
        QCOMPARE(updater(QStringLiteral("pacman -Sy firefox")), QStringLiteral("pacman"));

        QCOMPARE(updater(QStringLiteral("pacman -S --needed firefox")), QString());
        QCOMPARE(updater(QStringLiteral("pacman -Rns firefox")), QString());
        QCOMPARE(updater(QStringLiteral("pacman -Qu")), QString());
        QCOMPARE(updater(QStringLiteral("pacman -Sc")), QString());
        QCOMPARE(updater(QStringLiteral("paru -S spotify")), QString());
        QCOMPARE(updater(QStringLiteral("paru spotify")), QString());
        QCOMPARE(updater(QStringLiteral("yay -Y --gendb")), QString());
        QCOMPARE(updater(QStringLiteral("flatpak update -y")), QString());
        QCOMPARE(updater(QStringLiteral("echo pacman; asusctl profile -P Quiet")), QString());
    }

    void theProposalIsFoundInsideAShellCommand()
    {
        const QString json = QStringLiteral(R"({"title":"Night light","command":"kwriteconfig6 --key Active true"})");
        // Inside double quotes the JSON's own quotes are escaped.
        const QString escaped = QString(json).replace(QLatin1Char('"'), QStringLiteral("\\\""));
        for (const QString &line : {QStringLiteral("split-rock-change '%1'").arg(json),
                                    QStringLiteral("bash -lc \"split-rock-change '%1'\"").arg(escaped),
                                    QStringLiteral("/bin/sh -c \"cd ~ && split-rock-change '%1'\"").arg(escaped)}) {
            const auto change = ChangeProposal::fromCommandLine(line);
            QVERIFY2(change, qPrintable(line));
            QCOMPARE(change->title, QStringLiteral("Night light"));
        }
        QString why;
        QVERIFY(!ChangeProposal::fromCommandLine(QStringLiteral("ls -la"), &why));
        QVERIFY(!why.isEmpty());
    }

    void theCardShowsTheProposal()
    {
        ChangeProposal change;
        change.title = QStringLiteral("Quiet fans");
        change.why = QStringLiteral("They are loud");
        change.command = QStringLiteral("asusctl profile -P Quiet");
        change.undo = QStringLiteral("asusctl profile -P Balanced");
        change.admin = true;
        const QVariantMap card = ChangeCard::forProposal(change, QStringLiteral("Pretend")).toMap();
        QCOMPARE(card.value(QStringLiteral("heading")).toString(), QStringLiteral("Quiet fans"));
        QCOMPARE(card.value(QStringLiteral("available")).toBool(), true);
        QVERIFY(card.value(QStringLiteral("unavailable")).toString().isEmpty());
    }

    // A change to the person's own file: copied aside, changed, put back.
    void aFileChangeIsPutBackFromItsCopy()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("settings.conf"));
        const QString fresh = dir.filePath(QStringLiteral("new.conf"));
        write(file, "before\n");

        ChangeRunner runner(dir.filePath(QStringLiteral("changes")));
        QSignalSpy made(&runner, &ChangeRunner::made);
        ChangeProposal change;
        change.title = QStringLiteral("Write the setting");
        change.command = QStringLiteral("echo after > '%1' && echo new > '%2'").arg(file, fresh);
        change.files = {file, fresh};
        const QString id = runner.make(change);
        QVERIFY(!id.isEmpty());
        QVERIFY(runner.make(change).isEmpty()); // one at a time
        // The notebook keeps the ID, so it never reads as a card number.
        QVERIFY(!id.contains(QRegularExpression(QStringLiteral("\\d{9}"))));
        QVERIFY(neverWrittenReason(QStringLiteral("change %1").arg(id)).isEmpty());
        QTRY_COMPARE(made.size(), 1);
        auto record = made.first().first().value<ChangeRunner::Record>();
        QCOMPARE(record.status, QStringLiteral("done"));
        QCOMPARE(read(file), QByteArray("after\n"));
        QVERIFY(QFile::exists(fresh));
        QVERIFY(record.canUndo());

        // Kept on disk, with its way back.
        const auto kept = runner.record(id);
        QVERIFY(kept);
        QCOMPARE(kept->copies.size(), 2);
        QCOMPARE(runner.records().size(), 1);

        QSignalSpy undone(&runner, &ChangeRunner::undone);
        QVERIFY(runner.undo(id));
        QTRY_COMPARE(undone.size(), 1);
        record = undone.first().first().value<ChangeRunner::Record>();
        QCOMPARE(record.status, QStringLiteral("undone"));
        QCOMPARE(read(file), QByteArray("before\n"));
        QVERIFY(!QFile::exists(fresh));
        QVERIFY(!runner.record(id)->canUndo());
        QString why;
        QVERIFY(!runner.undo(id, &why));
        QVERIFY(!why.isEmpty());
    }

    // No files named: Undo runs the way back the assistant gave.
    void anUndoCommandIsTheWayBack()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mark = dir.filePath(QStringLiteral("on"));
        ChangeRunner runner(dir.filePath(QStringLiteral("changes")));
        QSignalSpy made(&runner, &ChangeRunner::made);
        QSignalSpy undone(&runner, &ChangeRunner::undone);
        ChangeProposal change;
        change.title = QStringLiteral("Turn it on");
        change.command = QStringLiteral("touch '%1'").arg(mark);
        change.undo = QStringLiteral("rm '%1'").arg(mark);
        const QString id = runner.make(change);
        QTRY_COMPARE(made.size(), 1);
        QVERIFY(QFile::exists(mark));
        QVERIFY(runner.undo(id));
        QTRY_COMPARE(undone.size(), 1);
        QVERIFY(!QFile::exists(mark));
    }

    void aFailedChangeIsRecordedAsFailed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ChangeRunner runner(dir.filePath(QStringLiteral("changes")));
        QSignalSpy made(&runner, &ChangeRunner::made);
        ChangeProposal change;
        change.title = QStringLiteral("Break");
        change.command = QStringLiteral("echo nope; exit 3");
        runner.make(change);
        QTRY_COMPARE(made.size(), 1);
        const auto record = made.first().first().value<ChangeRunner::Record>();
        QCOMPARE(record.status, QStringLiteral("failed"));
        QCOMPARE(record.exitCode, 3);
        QVERIFY(record.output.contains(QStringLiteral("nope")));
        QVERIFY(!record.canUndo());
    }

    // The check reads the live state back after the change, for the
    // assistant; a kwinrc setting is followed by KWin's reload.
    void theCheckReadsTheChangeBack()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mark = dir.filePath(QStringLiteral("on"));
        ChangeRunner runner(dir.filePath(QStringLiteral("changes")));
        QSignalSpy made(&runner, &ChangeRunner::made);
        ChangeProposal change;
        change.title = QStringLiteral("Turn it on");
        change.command = QStringLiteral("echo yes > '%1'").arg(mark);
        change.check = QStringLiteral("cat '%1'").arg(mark);
        const QString id = runner.make(change);
        QTRY_COMPARE(made.size(), 1);
        QCOMPARE(made.first().first().value<ChangeRunner::Record>().checked, QStringLiteral("yes"));
        QCOMPARE(runner.record(id)->checked, QStringLiteral("yes"));
        QVERIFY(!runner.busy());

        QVERIFY(ChangeRunner::reloadFor(change).isEmpty());
        change.command = QStringLiteral("kwriteconfig6 --file kwinrc --group NightColor --key Active true");
        QVERIFY(ChangeRunner::reloadFor(change).contains(QStringLiteral("reconfigure")));
        change.admin = true;
        QVERIFY(ChangeRunner::reloadFor(change).isEmpty());
    }

    // The assistant's way back applies the old setting live, so it comes
    // before the copies; the copies still put the files back when it fails.
    void theWayBackComesFirstAndTheCopiesCatchItsFailure()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(QStringLiteral("theme.conf"));
        write(file, "dark\n");
        ChangeRunner runner(dir.filePath(QStringLiteral("changes")));
        QSignalSpy made(&runner, &ChangeRunner::made);
        QSignalSpy undone(&runner, &ChangeRunner::undone);
        ChangeProposal change;
        change.title = QStringLiteral("Light theme");
        change.command = QStringLiteral("echo light > '%1'").arg(file);
        change.undo = QStringLiteral("echo dark-by-undo > '%1'").arg(file);
        change.files = {file};
        QString id = runner.make(change);
        QTRY_COMPARE(made.size(), 1);
        QVERIFY(runner.undo(id));
        QTRY_COMPARE(undone.size(), 1);
        QCOMPARE(read(file), QByteArray("dark-by-undo\n"));

        write(file, "dark\n");
        change.undo = QStringLiteral("exit 1");
        id = runner.make(change);
        QTRY_COMPARE(made.size(), 2);
        QCOMPARE(read(file), QByteArray("light\n"));
        QVERIFY(runner.undo(id));
        QTRY_COMPARE(undone.size(), 2);
        QCOMPARE(undone.last().first().value<ChangeRunner::Record>().status, QStringLiteral("undone"));
        QCOMPARE(read(file), QByteArray("dark\n"));
    }

    // The look is read outside the fence: the global theme and colour scheme
    // in use, and the installed ones, the person's own first.
    void theLookIsReadAsAWhole()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QDir().mkpath(dir.filePath(QStringLiteral("config")));
        write(dir.filePath(QStringLiteral("config/kdeglobals")),
              "[General]\nColorScheme=ShuffleDark\n\n[KDE]\nLookAndFeelPackage[$i]=org.shuffle.desktop\n");
        QDir().mkpath(dir.filePath(QStringLiteral("own/color-schemes")));
        write(dir.filePath(QStringLiteral("own/color-schemes/ShuffleDark.colors")), "[General]\nName=Shuffle Dark\n");
        QDir().mkpath(dir.filePath(QStringLiteral("system/color-schemes")));
        write(dir.filePath(QStringLiteral("system/color-schemes/BreezeLight.colors")), "[General]\nName=Breeze Light\n");
        write(dir.filePath(QStringLiteral("system/color-schemes/ShuffleDark.colors")), "[General]\nName=Older\n");
        QDir().mkpath(dir.filePath(QStringLiteral("system/plasma/look-and-feel/org.kde.breeze.desktop")));
        write(dir.filePath(QStringLiteral("system/plasma/look-and-feel/org.kde.breeze.desktop/metadata.json")),
              R"({"KPlugin":{"Name":"Breeze"}})");
        write(dir.filePath(QStringLiteral("config/kcminputrc")), "[Mouse]\ncursorTheme=Altas-Dark\n");
        QDir().mkpath(dir.filePath(QStringLiteral("own/icons/Altas-Dark/cursors")));
        write(dir.filePath(QStringLiteral("own/icons/Altas-Dark/index.theme")), "[Icon Theme]\nName=Altas Dark\n");
        QDir().mkpath(dir.filePath(QStringLiteral("system/icons/hicolor/48x48")));

        const Appearance now = Appearance::read(dir.filePath(QStringLiteral("config")),
                                                {dir.filePath(QStringLiteral("own")), dir.filePath(QStringLiteral("system"))});
        QCOMPARE(now.lookAndFeel, QStringLiteral("org.shuffle.desktop"));
        QCOMPARE(now.colorScheme, QStringLiteral("ShuffleDark"));
        QCOMPARE(now.schemes, (QStringList{QStringLiteral("ShuffleDark (Shuffle Dark)"), QStringLiteral("BreezeLight (Breeze Light)")}));
        QCOMPARE(now.looks, QStringList{QStringLiteral("org.kde.breeze.desktop (Breeze)")});
        QVERIFY(now.describe().contains(QStringLiteral("global theme org.shuffle.desktop, colour scheme ShuffleDark, pointer theme Altas-Dark")));
        QCOMPARE(now.cursors, QStringList{QStringLiteral("Altas-Dark (Altas Dark)")});

        // A name that isn't installed is caught before any card.
        // A loose name is the one installed name it fits.
        QCOMPARE(now.unknownName(QStringLiteral("plasma-apply-cursortheme Altas")), QString());
        QCOMPARE(now.resolve(QStringLiteral("plasma-apply-cursortheme Altas")), QStringLiteral("plasma-apply-cursortheme Altas-Dark"));
        QCOMPARE(now.resolve(QStringLiteral("plasma-apply-colorscheme 'breeze light' >/dev/null")),
                 QStringLiteral("plasma-apply-colorscheme 'breeze light' >/dev/null")); // a quoted phrase is left for the assistant
        QCOMPARE(now.resolve(QStringLiteral("plasma-apply-colorscheme breezelight")), QStringLiteral("plasma-apply-colorscheme BreezeLight"));
        QCOMPARE(now.unknownName(QStringLiteral("plasma-apply-cursortheme Bibata")), QStringLiteral("Bibata"));
        QCOMPARE(now.unknownName(QStringLiteral("plasma-apply-colorscheme 'breeze light'")), QStringLiteral("breeze light"));
        QCOMPARE(now.unknownName(QStringLiteral("plasma-apply-cursortheme 'Altas-Dark'")), QString());
        QCOMPARE(now.unknownName(QStringLiteral("plasma-apply-colorscheme BreezeLight && plasma-apply-lookandfeel -a org.kde.breeze.desktop")), QString());
        QCOMPARE(now.unknownName(QStringLiteral("plasma-apply-colorscheme BreezeDark")), QStringLiteral("BreezeDark"));
        QCOMPARE(now.unknownName(QStringLiteral("plasma-apply-colorscheme --list-schemes")), QString());

        // A theme change is undone by putting this look back, whatever way
        // back the assistant gave, and read back afterwards.
        ChangeProposal change;
        change.title = QStringLiteral("Light theme");
        change.command = QStringLiteral("plasma-apply-colorscheme BreezeLight");
        change.undo = QStringLiteral("plasma-apply-colorscheme BreezeDark");
        const ChangeProposal known = ChangeRunner::withKnownUndo(change, now);
        QVERIFY(known.undo.startsWith(QStringLiteral("plasma-apply-lookandfeel -a 'org.shuffle.desktop'")));
        QVERIFY(known.undo.contains(QStringLiteral("plasma-apply-colorscheme 'ShuffleDark'")));
        QVERIFY(known.undo.contains(QStringLiteral("plasma-apply-cursortheme 'Altas-Dark'")));
        QVERIFY(known.check.contains(QStringLiteral("kreadconfig6")));
        QVERIFY(ChangeRunner::undoPromise(known).contains(QStringLiteral("whole look")));
        change.command = QStringLiteral("kwriteconfig6 --file kwinrc --group NightColor --key Active true");
        QCOMPARE(ChangeRunner::withKnownUndo(change, now).undo, change.undo);
    }

    // The way back runs the desktop's own tools, here stand-ins, and fails
    // unless the colour scheme is back.
    void theLookIsPutBackAndChecked()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString bin = dir.filePath(QStringLiteral("bin"));
        QDir().mkpath(bin);
        const auto tool = [&](const QString &name, const QByteArray &body) {
            const QString path = QDir(bin).filePath(name);
            write(path, "#!/bin/sh\n" + body);
            QFile(path).setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        };
        tool(QStringLiteral("plasma-apply-lookandfeel"), "echo \"$2\" > \"$STATE/lnf\"; echo Breeze > \"$STATE/scheme\"\n");
        tool(QStringLiteral("plasma-apply-colorscheme"), "[ -n \"$BROKEN\" ] && exit 1; echo \"$1\" > \"$STATE/scheme\"\n");
        tool(QStringLiteral("kreadconfig6"), "cat \"$STATE/scheme\"\n");
        tool(QStringLiteral("plasma-apply-cursortheme"), "echo \"$1\" > \"$STATE/cursor\"\n");
        Appearance now;
        now.lookAndFeel = QStringLiteral("org.shuffle.desktop");
        now.colorScheme = QStringLiteral("ShuffleDark");

        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("PATH"), bin + QLatin1Char(':') + environment.value(QStringLiteral("PATH")));
        environment.insert(QStringLiteral("STATE"), dir.path());
        QProcess restore;
        restore.setProcessEnvironment(environment);
        restore.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), now.restoreCommand()});
        QVERIFY(restore.waitForFinished());
        QCOMPARE(restore.exitCode(), 0);
        QCOMPARE(read(dir.filePath(QStringLiteral("lnf"))), QByteArray("org.shuffle.desktop\n"));
        QCOMPARE(read(dir.filePath(QStringLiteral("scheme"))), QByteArray("ShuffleDark\n"));
        QCOMPARE(read(dir.filePath(QStringLiteral("cursor"))), QByteArray("breeze_cursors\n"));

        environment.insert(QStringLiteral("BROKEN"), QStringLiteral("1"));
        restore.setProcessEnvironment(environment);
        restore.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), now.restoreCommand()});
        QVERIFY(restore.waitForFinished());
        QVERIFY(restore.exitCode() != 0);
    }

    // A change still running when its time is up is stopped, and recorded
    // as not done, so Do it never waits for ever.
    void aChangeThatNeverFinishesIsStopped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ChangeRunner runner(dir.filePath(QStringLiteral("changes")));
        runner.setTimeLimit(300);
        QSignalSpy made(&runner, &ChangeRunner::made);
        ChangeProposal change;
        change.title = QStringLiteral("Wait");
        change.command = QStringLiteral("echo started; sleep 30");
        runner.make(change);
        QTRY_COMPARE_WITH_TIMEOUT(made.size(), 1, 5000);
        const auto record = made.first().first().value<ChangeRunner::Record>();
        QCOMPARE(record.status, QStringLiteral("failed"));
        QVERIFY(record.output.contains(QStringLiteral("started")));
        QVERIFY(record.output.contains(QStringLiteral("Stopped")));
        QVERIFY(!runner.busy());
    }

    // A system change goes through the elevating program; here none, and no
    // snapshots, so it runs as the test.
    void aSystemChangeGoesThroughTheElevatingProgram()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString mark = dir.filePath(QStringLiteral("system"));
        ChangeRunner runner(dir.filePath(QStringLiteral("changes")));
        runner.setElevate({QStringLiteral("env"), QStringLiteral("SPLIT_ROCK_ELEVATED=1")});
        runner.setSnapshots(false);
        QSignalSpy made(&runner, &ChangeRunner::made);
        ChangeProposal change;
        change.title = QStringLiteral("System");
        change.command = QStringLiteral("echo \"$SPLIT_ROCK_ELEVATED\" > '%1'").arg(mark);
        change.admin = true;
        runner.make(change);
        QTRY_COMPARE(made.size(), 1);
        const auto record = made.first().first().value<ChangeRunner::Record>();
        QCOMPARE(record.status, QStringLiteral("done"));
        QCOMPARE(read(mark), QByteArray("1\n"));
        QCOMPARE(record.pre, -1);
        QVERIFY(!record.canUndo());
    }
};

QTEST_GUILESS_MAIN(TestChange)
#include "test_change.moc"
