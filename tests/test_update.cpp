// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Update card's routine, read from what the computer's own programs print
// and run with scripts of the test's own: nothing here looks at or changes
// this computer.
#include "app/SystemUpdate.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QTimeZone>

#include "AdminStandIn.h"

#include <algorithm>

using namespace splitrock;

namespace {

QString fixture(const QString &name)
{
    QFile file(QStringLiteral(FIXTURES_DIR "/update/") + name);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

const QDateTime Now = QDateTime(QDate(2026, 10, 9), QTime(21, 0), QTimeZone::utc());

} // namespace

class TestUpdate : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theLookReadsWhatIsWaiting()
    {
        const SystemUpdate::Plan plan = SystemUpdate::readLook(fixture(QStringLiteral("look.txt")), Now);
        QCOMPARE(plan.system, 6);
        // The ignored one is left out; community packages never join the update.
        QCOMPARE(plan.community, 1);
        QCOMPARE(plan.apps, 2);
        // What a person notices, most noticeable first.
        QCOMPARE(plan.highlights, (QStringList{QStringLiteral("A new Linux kernel"), QStringLiteral("KDE Plasma 6.8.0"),
                                               QStringLiteral("Graphics drivers"), QStringLiteral("Firefox 144")}));
        QVERIFY(plan.restartLikely);
        QVERIFY(plan.snapshots);
        QVERIFY(!plan.snapPac);
        QVERIFY(plan.blockers.isEmpty());
        // Only the notice since the last update, on 1 October.
        QCOMPARE(plan.news.size(), 1);
        QCOMPARE(plan.news.first().link, QStringLiteral("https://archlinux.org/news/plasma-68-themes/"));
        QVERIFY(plan.warnings.first().startsWith(QStringLiteral("Arch posted a notice")));
        QVERIFY(plan.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("battery is at 72%")));
        // Shuffle is installed, and Plasma moves to 6.8.
        QVERIFY(plan.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("Shuffle has its update for Plasma 6.8")));
        QVERIFY(plan.desktopRisk);
    }

    void theLookListsEveryPackageBigPiecesFirst()
    {
        const SystemUpdate::Plan plan = SystemUpdate::readLook(fixture(QStringLiteral("look.txt")), Now);
        QStringList names;
        for (const SystemUpdate::Package &p : plan.list)
            names.append(p.name);
        // The kernel, Plasma, KWin and the graphics drivers, then the rest by
        // name, with the new dependency pacman would bring.
        QCOMPARE(names, (QStringList{QStringLiteral("linux-cachyos"), QStringLiteral("plasma-workspace"), QStringLiteral("kwin"),
                                     QStringLiteral("mesa"), QStringLiteral("firefox"), QStringLiteral("plasma-login-manager"),
                                     QStringLiteral("python")}));
        QCOMPARE(plan.list.at(1).piece, QStringLiteral("KDE Plasma"));
        QVERIFY(plan.list.at(1).step);
        // A fix, not a step.
        QVERIFY(!plan.list.at(0).step);
        QVERIFY(plan.list.at(4).piece.isEmpty());
        QVERIFY(plan.list.at(5).from.isEmpty());
        QCOMPARE(plan.list.at(0).size, qint64(157286400));
        QCOMPARE(plan.download, qint64(70254592 + 9437184 + 157286400 + 31457280 + 2097152 + 12582912));
        QCOMPARE(plan.appIds, (QStringList{QStringLiteral("org.gimp.GIMP"), QStringLiteral("org.videolan.VLC")}));

        // As the card reads it: versions without the epoch, sizes in words.
        const QVariantList rows = plan.toMap().value(QStringLiteral("list")).toList();
        const QVariantMap mesa = rows.at(3).toMap();
        QCOMPARE(mesa.value(QStringLiteral("from")).toString(), QStringLiteral("25.2.3"));
        QCOMPARE(mesa.value(QStringLiteral("to")).toString(), QStringLiteral("25.2.4"));
        QCOMPARE(mesa.value(QStringLiteral("size")).toString(), QStringLiteral("30 MB"));
        QCOMPARE(plan.toMap().value(QStringLiteral("pieces")).toList().size(), 4);
        QCOMPARE(plan.toMap().value(QStringLiteral("download")).toString(), QStringLiteral("270 MB"));

        // Headers and firmware are not the kernel.
        QString look = fixture(QStringLiteral("look.txt"));
        look.replace(QStringLiteral("python 3.13.7-1"), QStringLiteral("linux-cachyos-headers 6.17.1-1 -> 6.17.2-1\npython 3.13.7-1"));
        const SystemUpdate::Plan headers = SystemUpdate::readLook(look, Now);
        const auto it = std::find_if(headers.list.cbegin(), headers.list.cend(),
                                     [](const SystemUpdate::Package &p) { return p.name == QLatin1String("linux-cachyos-headers"); });
        QVERIFY(it != headers.list.cend());
        QVERIFY(it->piece.isEmpty());
    }

    void shuffleFromItsOwnFolderIsSeenToo()
    {
        QString look = fixture(QStringLiteral("look.txt"));
        look.replace(QStringLiteral("shuffle-plasma 1.0.0-1"), QStringLiteral("shuffle ~/.local/share"));
        QVERIFY(SystemUpdate::readLook(look, Now).desktopRisk);
        // Without Shuffle, Plasma 6.8 is only on the list.
        look.replace(QStringLiteral("shuffle ~/.local/share\n"), QString());
        const SystemUpdate::Plan plan = SystemUpdate::readLook(look, Now);
        QVERIFY(!plan.desktopRisk);
        QVERIFY(!plan.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("Shuffle")));
        // KWin moving a step alone asks too.
        look = fixture(QStringLiteral("look.txt"));
        look.replace(QStringLiteral("plasma-workspace 6.7.5-1 -> 6.8.0-1\n"), QString());
        const SystemUpdate::Plan kwin = SystemUpdate::readLook(look, Now);
        QVERIFY(kwin.desktopRisk);
        QVERIFY(kwin.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("KWin 6.8")));
    }

    void anUpdateWaitsForPowerSpaceAndTheLock()
    {
        QString look = fixture(QStringLiteral("look.txt"));
        look.replace(QStringLiteral("battery 72"), QStringLiteral("battery 31"));
        look.replace(QStringLiteral("48000000"), QStringLiteral("1048576"));
        look.replace(QStringLiteral("@@ busy\n"), QStringLiteral("@@ busy\nlocked\n"));
        const SystemUpdate::Plan plan = SystemUpdate::readLook(look, Now);
        QCOMPARE(plan.blockers.size(), 3);
        QVERIFY(plan.blockers.join(QLatin1Char(' ')).contains(QStringLiteral("battery is at 31%")));
        QVERIFY(plan.blockers.join(QLatin1Char(' ')).contains(QStringLiteral("1.0 GB is free")));
        QVERIFY(plan.blockers.join(QLatin1Char(' ')).contains(QStringLiteral("installing right now")));

        // Plugged in, nothing about the battery.
        look = fixture(QStringLiteral("look.txt")).replace(QStringLiteral("ac 0"), QStringLiteral("ac 1"));
        QVERIFY(!SystemUpdate::readLook(look, Now).warnings.join(QLatin1Char(' ')).contains(QStringLiteral("battery")));
    }

    void withoutRestorePointsItSaysSo()
    {
        QString look = fixture(QStringLiteral("look.txt"));
        look.replace(QStringLiteral("@@ snapshots\nyes\n"), QStringLiteral("@@ snapshots\n"));
        QVERIFY(SystemUpdate::readLook(look, Now).warnings.join(QLatin1Char(' ')).contains(QStringLiteral("restore points")));
        // snap-pac takes them itself.
        look = fixture(QStringLiteral("look.txt")).replace(QStringLiteral("@@ snapshots\nyes\n"), QStringLiteral("@@ snapshots\nsnap-pac\n"));
        const SystemUpdate::Plan plan = SystemUpdate::readLook(look, Now);
        QVERIFY(plan.snapPac);
        QVERIFY(!plan.warnings.join(QLatin1Char(' ')).contains(QStringLiteral("restore points")));
    }

    void nothingWaitingIsNothing()
    {
        const SystemUpdate::Plan plan = SystemUpdate::readLook(QStringLiteral("@@ system\n@@ community\n@@ apps\n@@ end\n"), Now);
        QVERIFY(plan.nothing());
        // Where checkupdates is missing the count is unknown, not nothing.
        QVERIFY(!SystemUpdate::readLook(QStringLiteral("@@ system\n@@ nochecker\n@@ apps\n"), Now).nothing());
    }

    void pacmansLinesBecomeProgress()
    {
        int done = 0;
        int total = 0;
        QString now;
        for (const QString &line : fixture(QStringLiteral("update.out")).split(QLatin1Char('\n'))) {
            const SystemUpdate::Step step = SystemUpdate::readStep(line, done, total);
            if (step.total > 0)
                total = step.total;
            if (step.counts)
                ++done;
            if (!step.now.isEmpty())
                now = step.now;
            if (line.startsWith(QLatin1String("upgrading kwin")))
                QCOMPARE(step.now, QStringLiteral("Installing 2 of 6: kwin"));
        }
        QCOMPARE(total, 6);
        QCOMPARE(done, 6);
        QCOMPARE(now, QStringLiteral("Finishing up"));
    }

    void aFailureIsSaidPlainly()
    {
        QVERIFY(SystemUpdate::whyFailed(QString(), 126).contains(QStringLiteral("password prompt was closed")));
        QVERIFY(SystemUpdate::whyFailed(QStringLiteral("error: failed to init transaction (unable to lock database)"), 1)
                    .contains(QStringLiteral("Something else was installing")));
        QVERIFY(SystemUpdate::whyFailed(QStringLiteral("error: failed retrieving file 'x' : Could not resolve host"), 1)
                    .contains(QStringLiteral("internet")));
        QVERIFY(SystemUpdate::whyFailed(QStringLiteral("error: failed to commit transaction (conflicting files)"), 1)
                    .contains(QStringLiteral("same file")));
        QVERIFY(SystemUpdate::whyFailed(QStringLiteral("error: failed to prepare transaction (could not satisfy dependencies)"), 1)
                    .contains(QStringLiteral("outside the official shelf")));
        QVERIFY(!SystemUpdate::whyFailed(QStringLiteral("something new"), 1).isEmpty());
    }

    void theWholeUpdateRuns()
    {
        SystemUpdate update;
        update.setElevate({});
        update.setLookScript(QStringLiteral("cat '" FIXTURES_DIR "/update/look.txt'"));
        update.setAdmin(adminStandIn(QStringLiteral("cat '" FIXTURES_DIR "/update/update.out'; echo split-rock-pre=41; echo split-rock-post=42")));
        update.setAppsScript(QStringLiteral("echo Updating apps"));
        update.setAfterScript(QStringLiteral("printf '@@ kernel\\nrestart\\n@@ settings\\n/etc/pacman.conf.pacnew\\n@@ failed\\n@@ end\\n'"));

        update.check();
        QCOMPARE(update.stage(), QStringLiteral("looking"));
        QTRY_COMPARE(update.stage(), QStringLiteral("ready"));
        QCOMPARE(update.plan().system, 6);

        QSignalSpy finished(&update, &SystemUpdate::finished);
        update.start();
        // Plasma moves a step under Shuffle: asked once more first.
        QCOMPARE(update.stage(), QStringLiteral("confirming"));
        update.confirm();
        // Then the list is looked at again before anything changes.
        QCOMPARE(update.stage(), QStringLiteral("rechecking"));
        // Never closed while it runs.
        update.close();
        QCOMPARE(update.stage(), QStringLiteral("rechecking"));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(update.stage(), QStringLiteral("done"));
        const SystemUpdate::Result result = update.result();
        QVERIFY(result.ok);
        QCOMPARE(result.installed, 6);
        QCOMPARE(result.pre, 41);
        QCOMPARE(result.post, 42);
        QVERIFY(result.restart);
        QVERIFY(result.appsUpdated);
        QCOMPARE(result.settingsFiles, QStringList{QStringLiteral("/etc/pacman.conf.pacnew")});
        QVERIFY(update.log().contains(QStringLiteral("upgrading python...")));
        update.close();
        QCOMPARE(update.stage(), QString());
    }

    void aFailedUpdateSaysWhy()
    {
        SystemUpdate update;
        update.setElevate({});
        update.setLookScript(QStringLiteral("cat '" FIXTURES_DIR "/update/look.txt'"));
        update.setAdmin(adminStandIn(QStringLiteral("echo 'error: failed to init transaction (unable to lock database)'; exit 1")));
        update.check();
        QTRY_COMPARE(update.stage(), QStringLiteral("ready"));
        update.start();
        update.confirm();
        QTRY_COMPARE(update.stage(), QStringLiteral("failed"));
        QVERIFY(!update.result().ok);
        QVERIFY(update.result().why.contains(QStringLiteral("Something else was installing")));
    }

    void anUpdateThatChangedSinceItWasReadIsShownAgain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString look = fixture(QStringLiteral("look.txt"));
        QFile first(dir.filePath(QStringLiteral("first.txt")));
        QVERIFY(first.open(QIODevice::WriteOnly));
        first.write(look.toUtf8());
        first.close();
        QFile second(dir.filePath(QStringLiteral("second.txt")));
        QVERIFY(second.open(QIODevice::WriteOnly));
        second.write(QString(look).replace(QStringLiteral("python 3.13.7-1 -> 3.13.8-1"),
                                           QStringLiteral("python 3.13.7-1 -> 3.13.8-1\nqt6-base 6.9.3-1 -> 6.10.0-1")).toUtf8());
        second.close();
        QFile updated(dir.filePath(QStringLiteral("updated")));

        SystemUpdate update;
        update.setElevate({});
        update.setLookScript(QStringLiteral("cd '%1' && if [ -e seen ]; then cat second.txt; else touch seen; cat first.txt; fi").arg(dir.path()));
        update.setAdmin(adminStandIn(QStringLiteral("touch '%1'").arg(updated.fileName())));
        update.setAppsScript(QStringLiteral("true"));
        update.setAfterScript(QStringLiteral("echo '@@ end'"));
        update.check();
        QTRY_COMPARE(update.stage(), QStringLiteral("ready"));
        QCOMPARE(update.plan().system, 6);
        update.start();
        update.confirm();
        // Qt arrived in between: back to the list, nothing installed.
        QTRY_COMPARE(update.stage(), QStringLiteral("ready"));
        QVERIFY(update.plan().changed);
        QCOMPARE(update.plan().system, 7);
        QVERIFY(!updated.exists());
        QVERIFY(update.planMap().value(QStringLiteral("changed")).toBool());

        // Read again, and the same list: it updates.
        update.start();
        update.confirm();
        QTRY_COMPARE(update.stage(), QStringLiteral("done"));
        QVERIFY(updated.exists());
    }

    void aBlockedUpdateDoesNotStart()
    {
        QString look = fixture(QStringLiteral("look.txt")).replace(QStringLiteral("battery 72"), QStringLiteral("battery 20"));
        SystemUpdate update;
        update.setElevate({});
        update.setLookScript(QStringLiteral("cat <<'LOOK'\n") + look + QStringLiteral("\nLOOK"));
        update.setAdmin(adminStandIn(QStringLiteral("exit 99")));
        update.check();
        QTRY_COMPARE(update.stage(), QStringLiteral("ready"));
        update.start();
        QCOMPARE(update.stage(), QStringLiteral("ready"));
    }
};

QTEST_MAIN(TestUpdate)
#include "test_update.moc"
