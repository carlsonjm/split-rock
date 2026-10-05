// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Change.h"
#include "app/ChangeCard.h"
#include "app/ChangeRunner.h"
#include "app/NeverWritten.h"

#include <QFile>
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
