// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Update card's routine, read from what the computer's own programs print
// and run with scripts of the test's own: nothing here looks at or changes
// this computer.
#include "app/Conversation.h"
#include "app/SystemUpdate.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

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
        update.setUpdateScript(QStringLiteral("cat '" FIXTURES_DIR "/update/update.out'; echo split-rock-pre=41; echo split-rock-post=42"));
        update.setAppsScript(QStringLiteral("echo Updating apps"));
        update.setAfterScript(QStringLiteral("printf '@@ kernel\\nrestart\\n@@ settings\\n/etc/pacman.conf.pacnew\\n@@ failed\\n@@ end\\n'"));

        update.check();
        QCOMPARE(update.stage(), QStringLiteral("looking"));
        QTRY_COMPARE(update.stage(), QStringLiteral("ready"));
        QCOMPARE(update.plan().system, 6);

        QSignalSpy finished(&update, &SystemUpdate::finished);
        update.start();
        QCOMPARE(update.stage(), QStringLiteral("updating"));
        // Never closed while it runs.
        update.close();
        QCOMPARE(update.stage(), QStringLiteral("updating"));
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
        update.setUpdateScript(QStringLiteral("echo 'error: failed to init transaction (unable to lock database)'; exit 1"));
        update.check();
        QTRY_COMPARE(update.stage(), QStringLiteral("ready"));
        update.start();
        QTRY_COMPARE(update.stage(), QStringLiteral("failed"));
        QVERIFY(!update.result().ok);
        QVERIFY(update.result().why.contains(QStringLiteral("Something else was installing")));
    }

    void anUpdateProposedForTheCardOpensTheUpdateCard()
    {
        Conversation c({});
        c.systemUpdate()->setLookScript(QStringLiteral("cat '" FIXTURES_DIR "/update/look.txt'"));
        ChangeProposal change;
        change.title = QStringLiteral("Update everything");
        change.command = QStringLiteral("pacman -Syu --noconfirm");
        change.admin = true;
        c.propose(change);
        // No Do it card: the Update card looks instead.
        QVERIFY(!c.cardOpen());
        QCOMPARE(c.systemUpdate()->stage(), QStringLiteral("looking"));
        QTRY_COMPARE(c.systemUpdate()->stage(), QStringLiteral("ready"));

        // A single package is still an ordinary change.
        c.systemUpdate()->close();
        change.command = QStringLiteral("pacman -S --needed firefox");
        c.propose(change);
        QVERIFY(c.cardOpen());
        QCOMPARE(c.systemUpdate()->stage(), QString());
    }

    void aBlockedUpdateDoesNotStart()
    {
        QString look = fixture(QStringLiteral("look.txt")).replace(QStringLiteral("battery 72"), QStringLiteral("battery 20"));
        SystemUpdate update;
        update.setElevate({});
        update.setLookScript(QStringLiteral("cat <<'LOOK'\n") + look + QStringLiteral("\nLOOK"));
        update.setUpdateScript(QStringLiteral("exit 99"));
        update.check();
        QTRY_COMPARE(update.stage(), QStringLiteral("ready"));
        update.start();
        QCOMPARE(update.stage(), QStringLiteral("ready"));
    }
};

QTEST_MAIN(TestUpdate)
#include "test_update.moc"
