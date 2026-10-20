// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core, the front desk: what it makes of its looks at the drive, when it
// speaks and where, the files it keeps, and its bus interface. Every look and
// every clock here is the test's own; nothing looks at this computer's drive,
// and files are made only inside the test's own home.
#include "core/Attention.h"
#include "core/Core.h"
#include "core/CoreService.h"
#include "core/Health.h"
#include "core/Storage.h"
#include "core/Updates.h"
#include "core/Windows.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace splitrock::core;

namespace {

constexpr qint64 Gb = 1024 * 1024;
const QDateTime Start = QDateTime(QDate(2026, 10, 15), QTime(9, 0), QTimeZone::utc());

// A 500 GB drive, this full.
Sample at(const QDateTime &when, double percent)
{
    return {when, 500 * Gb, qint64(500 * Gb * (100.0 - percent) / 100.0)};
}

QString read(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

// Core with a drive and a clock the test moves.
struct Desk {
    QTemporaryDir dir;
    QDateTime now = Start;
    double percent = 50;
    bool doNotDisturb = false;
    QStringList ticker;
    QStringList topics;
    QStringList opened;
    std::unique_ptr<Core> core;

    Desk() { make(); }

    void make()
    {
        Core::Hooks hooks;
        hooks.look = [this](const QDateTime &when) { return at(when, percent); };
        hooks.now = [this] { return now; };
        hooks.doNotDisturb = [this] { return doNotDisturb; };
        hooks.notify = [this](const QString &topic, const QString &summary, const QString &body) {
            ticker.append(summary + QStringLiteral(" | ") + body);
            topics.append(topic);
        };
        hooks.openCard = [this](const QString &card, const QString &query) {
            opened.append(query.isEmpty() ? card : card + QStringLiteral(": ") + query);
        };
        core = std::make_unique<Core>(dir.filePath(QStringLiteral("core")), dir.filePath(QStringLiteral("corerc")), hooks);
    }

    void look(double full, int minutesLater = 30)
    {
        now = now.addSecs(minutesLater * 60);
        percent = full;
        core->check();
    }

    // A look for updates: this many waiting, the last full update this many
    // days ago (none when negative).
    void updates(int waiting, int daysSince, int minutesLater = 30)
    {
        now = now.addSecs(minutesLater * 60);
        UpdatesLook seen;
        seen.at = now;
        seen.known = true;
        seen.waiting = waiting;
        if (daysSince >= 0)
            seen.lastUpdate = now.addDays(-daysSince);
        core->seeUpdates(seen);
    }

    // A look at the computer's health, with these problems by id, a day later
    // unless said otherwise.
    void health(const QStringList &problems, int minutesLater = 60 * 24)
    {
        now = now.addSecs(minutesLater * 60);
        QString printed = QStringLiteral("good\tbattery\tBattery\tHolds 86%\n");
        for (const QString &id : problems)
            printed += QStringLiteral("problem\t%1\t%2\tIt went wrong.\n").arg(id, id.left(1).toUpper() + id.mid(1));
        core->seeHealth(readHealthFacts(printed, now));
    }

    void setNotice(const QString &notice)
    {
        QSettings settings(dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("General/Notice"), notice);
    }

    QVariantMap fact() const { return core->facts().value(0); }
    QVariantMap fact(const QString &topic) const
    {
        for (const QVariantMap &fact : core->facts()) {
            if (fact.value(QStringLiteral("topic")).toString() == topic)
                return fact;
        }
        return {};
    }
    QString logbook() const { return read(dir.filePath(QStringLiteral("core/logbook.txt"))); }
};

} // namespace

class TestCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theDriveIsJudgedByHowFullAndHowFast()
    {
        StorageFact fact = judge({at(Start, 70)});
        QVERIFY(fact.known);
        QCOMPARE(fact.percent, 70);
        QCOMPARE(fact.level, Level::Quiet);
        QCOMPARE(fact.line, QStringLiteral("70% full"));
        QCOMPARE(fact.detail, QStringLiteral("150 GB free"));
        QVERIFY(!fact.trendKnown);

        QCOMPARE(judge({at(Start, 74)}).level, Level::Quiet);
        QCOMPARE(judge({at(Start, 75)}).level, Level::Ticker);
        QCOMPARE(judge({at(Start, 86)}).level, Level::Ticker);
        QCOMPARE(judge({at(Start, 91)}).level, Level::Island);

        // 82% and filling 10 GB a day: full in 9 days, which is fast.
        const StorageFact fast = judge({at(Start, 80), at(Start.addDays(1), 82)});
        QVERIFY(fast.trendKnown);
        QCOMPARE(fast.growthKbPerDay, 10 * Gb);
        QCOMPARE(fast.daysToFull, 9);
        QCOMPARE(fast.level, Level::Island);
        QCOMPARE(fast.detail, QStringLiteral("90 GB free · filling about 10 GB a day"));

        // Less than half a day of looks says nothing about the trend.
        QVERIFY(!judge({at(Start, 80), at(Start.addSecs(3600), 82)}).trendKnown);
        // Shrinking is not filling.
        QCOMPARE(judge({at(Start, 80), at(Start.addDays(1), 70)}).daysToFull, -1);
        // A drive that can't be read is not a fact.
        QVERIFY(!judge({Sample{Start, 0, 0}}).known);
    }

    void itSpeaksOnceAtEachLevelAndOnlyWhenWorse()
    {
        Desk d;
        d.look(70);
        QVERIFY(d.ticker.isEmpty());

        d.look(76);
        QCOMPARE(d.ticker.size(), 1);
        QCOMPARE(d.ticker.first(), QStringLiteral("Storage · 76% full | 120 GB free"));
        // Time passing says nothing new.
        d.look(76, 600);
        d.look(78);
        QCOMPARE(d.ticker.size(), 1);
        // Worse by the step: once more.
        d.look(79);
        QCOMPARE(d.ticker.size(), 2);

        // The island, once; not again the same day, even worse.
        d.look(91);
        QCOMPARE(d.fact().value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QCOMPARE(d.ticker.size(), 2);
        QVERIFY(d.core->act(QStringLiteral("storage"), QStringLiteral("not-now")));
        QCOMPARE(d.fact().value(QStringLiteral("attention")).toString(), QStringLiteral("none"));
        d.look(94);
        QCOMPARE(d.fact().value(QStringLiteral("attention")).toString(), QStringLiteral("none"));
        // Past the day's allowance the ticker carries it instead.
        QCOMPARE(d.ticker.size(), 3);

        // Freed: it settles, and the next fill speaks again.
        d.look(60);
        QVERIFY(d.logbook().contains(QStringLiteral("Settled at 60% full")));
        d.look(76, 60 * 24 * 8);
        QCOMPARE(d.ticker.size(), 4);
    }

    // Storage's island allowance is its own, kept with its own record.
    void theAllowanceBelongsToTheTopic()
    {
        Desk d;
        d.look(91);
        const QString saved = read(d.dir.filePath(QStringLiteral("core/attention.ini")));
        QVERIFY(saved.contains(QStringLiteral("[Storage]")));
        QVERIFY(saved.contains(QStringLiteral("IslandsToday=1")));
        QVERIFY(!saved.contains(QStringLiteral("[Islands]")));
    }

    void notNowQuietsItUntilItWorsens()
    {
        Desk d;
        d.look(91);
        QCOMPARE(d.fact().value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QVERIFY(d.core->act(QStringLiteral("storage"), QStringLiteral("not-now")));
        QVERIFY(d.logbook().contains(QStringLiteral("You said not now; quiet until 94% full.")));
        d.look(93, 60 * 25);
        QVERIFY(d.ticker.isEmpty());
        d.look(94);
        QCOMPARE(d.fact().value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QVERIFY(!d.core->act(QStringLiteral("storage"), QStringLiteral("delete-everything")));
        QVERIFY(!d.core->act(QStringLiteral("weather"), QStringLiteral("open")));
    }

    void openingTheIslandOpensFreeUpSpace()
    {
        Desk d;
        QSignalSpy changed(d.core.get(), &Core::factChanged);
        d.look(92);
        QCOMPARE(changed.size(), 1);
        QVERIFY(d.core->act(QStringLiteral("storage"), QStringLiteral("open")));
        QCOMPARE(d.opened, QStringList{QStringLiteral("space")});
        QCOMPARE(changed.size(), 2);
        QCOMPARE(changed.last().first().toMap().value(QStringLiteral("attention")).toString(), QStringLiteral("none"));
        QVERIFY(d.logbook().contains(QStringLiteral("You opened Free up space.")));
    }

    void doNotDisturbLeavesOnlyTheTicker()
    {
        Desk d;
        d.doNotDisturb = true;
        d.look(92);
        QCOMPARE(d.fact().value(QStringLiteral("attention")).toString(), QStringLiteral("none"));
        QCOMPARE(d.ticker.size(), 1);
    }

    void anIslandNobodyTouchesClosesAfterADay()
    {
        Desk d;
        d.look(92);
        d.look(92, 60 * 25);
        QCOMPARE(d.fact().value(QStringLiteral("attention")).toString(), QStringLiteral("none"));
        QVERIFY(d.logbook().contains(QStringLiteral("went unanswered for a day")));
    }

    void whatItKeepsIsReadableAndSurvivesARestart()
    {
        Desk d;
        d.look(70);
        d.look(91);
        const QString tsv = read(d.dir.filePath(QStringLiteral("core/storage.tsv")));
        QCOMPARE(tsv.count(QLatin1Char('\n')), 2);
        QVERIFY(tsv.startsWith(QStringLiteral("2026-10-15T09:30:00Z\t524288000\t")));
        QVERIFY(d.logbook().contains(QStringLiteral("Storage  Showed the island: 91% full, 45 GB free.")));
        // The first look after a start is written down even when Core says nothing.
        QCOMPARE(d.logbook().count(QStringLiteral("First look since starting: 70% full")), 1);

        // A new start remembers the fact and what was said, so it says nothing twice.
        d.make();
        QCOMPARE(d.fact().value(QStringLiteral("percent")).toInt(), 91);
        QCOMPARE(d.fact().value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        d.look(91);
        QVERIFY(d.ticker.isEmpty());
        QCOMPARE(d.logbook().count(QStringLiteral("First look since starting")), 2);
    }

    void itKeepsNinetyDays()
    {
        Desk d;
        d.look(50);
        d.core->act(QStringLiteral("storage"), QStringLiteral("not-now"));
        d.now = d.now.addDays(Core::KeepDays + 1);
        d.look(51);
        d.make();
        const QString tsv = read(d.dir.filePath(QStringLiteral("core/storage.tsv")));
        QCOMPARE(tsv.count(QLatin1Char('\n')), 1);
        QVERIFY(!d.logbook().contains(QStringLiteral("not now")));
    }

    void aTopicSwitchedOffIsSilentAndAbsent()
    {
        Desk d;
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Storage/Enabled"), false);
        }
        d.look(95);
        QVERIFY(d.core->facts().isEmpty());
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(!d.core->act(QStringLiteral("storage"), QStringLiteral("open")));
        QVERIFY(!QFile::exists(d.dir.filePath(QStringLiteral("core/storage.tsv"))));
    }

    void searchOpensEachUpkeepCard()
    {
        Desk d;
        QStringList ids;
        for (const QVariantMap &action : Core::actions()) {
            ids.append(action.value(QStringLiteral("id")).toString());
            QVERIFY(!action.value(QStringLiteral("title")).toString().isEmpty());
            QVERIFY(!action.value(QStringLiteral("keywords")).toStringList().isEmpty());
        }
        QCOMPARE(ids, (QStringList{QStringLiteral("update"), QStringLiteral("space"), QStringLiteral("tidy"), QStringLiteral("install"),
                                   QStringLiteral("apps"), QStringLiteral("health"), QStringLiteral("changes"), QStringLiteral("restore"),
                                   QStringLiteral("community")}));
        QVERIFY(d.core->openAction(QStringLiteral("health"), QStringLiteral("health")));
        // Only Install an app takes what was typed.
        QVERIFY(d.core->openAction(QStringLiteral("install"), QStringLiteral(" install pdf editor ")));
        QVERIFY(d.core->openAction(QStringLiteral("apps"), QStringLiteral("uninstall")));
        // A Windows installer's name asks for its program.
        QVERIFY(d.core->openAction(QStringLiteral("install"), QStringLiteral("ZoomInstaller.exe")));
        QVERIFY(!d.core->openAction(QStringLiteral("format-the-disk"), QString()));
        // Search's Uninstall names the app by its desktop file id alone.
        QVERIFY(d.core->removeApp(QStringLiteral("org.videolan.vlc.desktop")));
        QVERIFY(!d.core->removeApp(QStringLiteral("../vlc.desktop")));
        QVERIFY(!d.core->removeApp(QStringLiteral("vlc; rm -rf ~")));
        QCOMPARE(d.opened, (QStringList{QStringLiteral("health"), QStringLiteral("install: install pdf editor"),
                                        QStringLiteral("apps"), QStringLiteral("install: Zoom"),
                                        QStringLiteral("uninstall: org.videolan.vlc.desktop")}));
        QVERIFY(d.logbook().contains(QStringLiteral("Search  You opened Health check.")));
        QVERIFY(d.logbook().contains(QStringLiteral("Search  You asked to remove org.videolan.vlc.")));
    }

    void updatesAreJudgedByDaysBehind()
    {
        UpdatesLook look;
        look.at = Start;
        QVERIFY(!judge(look, QDateTime()).known);

        look.known = true;
        look.waiting = 12;
        look.lastUpdate = Start.addDays(-9);
        UpdatesFact fact = judge(look, QDateTime());
        QVERIFY(fact.known);
        QCOMPARE(fact.daysBehind, 9);
        QCOMPARE(fact.level, Level::Ticker);
        QCOMPARE(fact.line, QStringLiteral("12 waiting"));
        QCOMPARE(fact.detail, QStringLiteral("Last updated 9 days ago"));

        look.lastUpdate = Start.addDays(-6);
        QCOMPARE(judge(look, QDateTime()).level, Level::Quiet);
        look.lastUpdate = Start.addDays(-21);
        QCOMPARE(judge(look, QDateTime()).level, Level::Island);

        // Nothing waiting is quiet however long ago the last update was.
        look.waiting = 0;
        fact = judge(look, QDateTime());
        QCOMPARE(fact.level, Level::Quiet);
        QCOMPARE(fact.line, QStringLiteral("Up to date"));
        QCOMPARE(fact.daysBehind, 0);

        // No record of updates: counted from when Core first saw them waiting.
        look.waiting = 3;
        look.lastUpdate = QDateTime();
        fact = judge(look, Start.addDays(-8));
        QCOMPARE(fact.level, Level::Ticker);
        QCOMPARE(fact.detail, QStringLiteral("Waiting for 8 days"));

        // Notice moves the marks.
        look.lastUpdate = Start.addDays(-4);
        QCOMPARE(judge(look, QDateTime(), UpdatesLimits{3, 10}).level, Level::Ticker);
        QCOMPARE(judge(look, QDateTime(), UpdatesLimits{21, 45}).level, Level::Quiet);
    }

    void theCheckersAreReadPlainly()
    {
        QCOMPARE(countLines(QStringLiteral("linux 6.17.1-1 -> 6.17.2-1\nmesa 25.2.4-1 -> 25.2.5-1\n\n")), 2);
        QCOMPARE(countLines(QString()), 0);
        const QString log = QStringLiteral(
            "[2026-10-01T10:00:00-0500] [PACMAN] Running 'pacman -Syu'\n"
            "[2026-10-01T10:00:01-0500] [PACMAN] starting full system upgrade\n"
            "[2026-10-03T08:00:00-0500] [PACMAN] Running 'pacman -S gimp'\n"
            "[2026-10-09T21:30:00-0500] [PACMAN] starting full system upgrade\n"
            "[2026-10-09T21:31:00-0500] [ALPM] upgraded linux (6.17.1-1 -> 6.17.2-1)\n");
        QCOMPARE(lastFullUpdate(log), QDateTime(QDate(2026, 10, 10), QTime(2, 30), QTimeZone::utc()));
        QVERIFY(!lastFullUpdate(QStringLiteral("[2026-10-03T08:00:00-0500] [PACMAN] Running 'pacman -S gimp'\n")).isValid());
    }

    void updatesSpeakAWeekWorseAtATime()
    {
        Desk d;
        d.updates(12, 2);
        QVERIFY(d.ticker.isEmpty());
        QCOMPARE(d.fact(QStringLiteral("updates")).value(QStringLiteral("line")).toString(), QStringLiteral("12 waiting"));
        QVERIFY(d.logbook().contains(QStringLiteral("Updates  First look since starting: 12 waiting, Last updated 2 days ago.")));

        d.updates(14, 7);
        QCOMPARE(d.ticker, QStringList{QStringLiteral("Updates · 14 waiting | Last updated 7 days ago")});
        QCOMPARE(d.topics, QStringList{QStringLiteral("updates")});
        // More updates on the same day are not worse; a week later is.
        d.updates(20, 13);
        QCOMPARE(d.ticker.size(), 1);
        d.updates(20, 14);
        QCOMPARE(d.ticker.size(), 2);

        // Three weeks behind: the island, Updates' own, beside Storage's.
        d.updates(25, 21);
        QCOMPARE(d.fact(QStringLiteral("updates")).value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        d.look(91);
        QCOMPARE(d.fact(QStringLiteral("storage")).value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QCOMPARE(d.core->facts().size(), 2);

        QVERIFY(d.core->act(QStringLiteral("updates"), QStringLiteral("open")));
        QCOMPARE(d.opened, QStringList{QStringLiteral("update")});
        QVERIFY(d.logbook().contains(QStringLiteral("Updates  You opened Check for updates.")));

        // Updated: settled, and the next wait starts over.
        d.updates(0, 0);
        QVERIFY(d.logbook().contains(QStringLiteral("Updates  Settled at Up to date, Last updated today.")));
        QCOMPARE(d.fact(QStringLiteral("updates")).value(QStringLiteral("level")).toString(), QStringLiteral("quiet"));
        d.updates(5, 7, 60 * 24 * 7);
        QCOMPARE(d.ticker.last(), QStringLiteral("Updates · 5 waiting | Last updated 7 days ago"));
    }

    void notNowQuietsUpdatesForAWeek()
    {
        Desk d;
        d.updates(30, 22);
        QVERIFY(d.core->act(QStringLiteral("updates"), QStringLiteral("not-now")));
        QVERIFY(d.logbook().contains(QStringLiteral("Updates  You said not now; quiet until 29 days behind.")));
        d.updates(30, 28, 60 * 24 * 2);
        QVERIFY(d.ticker.isEmpty());
        QCOMPARE(d.fact(QStringLiteral("updates")).value(QStringLiteral("attention")).toString(), QStringLiteral("none"));
        d.updates(30, 29);
        QCOMPARE(d.fact(QStringLiteral("updates")).value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
    }

    // Updated before Core said anything: the logbook still says so.
    void updatingQuietlyIsWrittenDown()
    {
        Desk d;
        d.updates(3, 2);
        QVERIFY(d.ticker.isEmpty());
        d.updates(0, 0);
        QCOMPARE(d.logbook().count(QStringLiteral("Updates  Settled at Up to date, Last updated today.")), 1);
        d.updates(0, 0);
        QCOMPARE(d.logbook().count(QStringLiteral("Settled at Up to date")), 1);
    }

    void aFailedLookLeavesTheLastFact()
    {
        Desk d;
        d.updates(4, 8);
        d.core->seeUpdates(UpdatesLook{d.now.addSecs(60), false, 0, {}});
        QCOMPARE(d.fact(QStringLiteral("updates")).value(QStringLiteral("waiting")).toInt(), 4);
        QCOMPARE(d.ticker.size(), 1);
    }

    void updatesSurviveARestart()
    {
        Desk d;
        d.updates(9, 10);
        d.make();
        QCOMPARE(d.fact(QStringLiteral("updates")).value(QStringLiteral("line")).toString(), QStringLiteral("9 waiting"));
        d.updates(9, 10);
        QCOMPARE(d.ticker.size(), 1);
        const QString saved = read(d.dir.filePath(QStringLiteral("core/attention.ini")));
        QVERIFY(saved.contains(QStringLiteral("[Updates]")));
        QVERIFY(saved.contains(QStringLiteral("Waiting=9")));
    }

    void theNoticeSettingMovesEveryTopicsMarks()
    {
        Desk d;
        QCOMPARE(d.core->notice(), 50);
        d.setNotice(QStringLiteral("quiet"));
        QCOMPARE(d.core->storageLimits().ticker, 85);
        QCOMPARE(d.core->updatesLimits().ticker, 21);
        d.look(80);
        d.updates(10, 10);
        QVERIFY(d.ticker.isEmpty());

        d.setNotice(QStringLiteral("eager"));
        QCOMPARE(d.core->storageLimits().island, 85);
        QCOMPARE(d.core->updatesLimits().island, 10);
        d.look(80);
        d.updates(10, 4);
        QCOMPARE(d.topics, (QStringList{QStringLiteral("storage"), QStringLiteral("updates")}));

        // A change is felt at once, without waiting for the next look, and
        // without a new look at the drive.
        Desk e;
        e.look(72);
        e.updates(10, 5);
        QVERIFY(e.ticker.isEmpty());
        e.setNotice(QStringLiteral("eager"));
        e.core->reconsider();
        e.core->reconsider();
        QCOMPARE(e.topics, (QStringList{QStringLiteral("storage"), QStringLiteral("updates")}));
        QCOMPARE(read(e.dir.filePath(QStringLiteral("core/storage.tsv"))).count(QLatin1Char('\n')), 1);

        // Anything else reads as the default.
        d.setNotice(QStringLiteral("loud"));
        QCOMPARE(d.core->notice(), 50);
        QCOMPARE(d.core->storageLimits().ticker, 75);
    }

    void theNoticeSliderMovesTheMarksBetweenItsEnds()
    {
        Desk d;
        // The ends and the middle are the three steps it replaced.
        d.setNotice(QStringLiteral("0"));
        QCOMPARE(d.core->storageLimits().ticker, 85);
        QCOMPARE(d.core->updatesLimits().island, 45);
        d.setNotice(QStringLiteral("50"));
        QCOMPARE(d.core->storageLimits().island, 90);
        QCOMPARE(d.core->updatesLimits().ticker, 7);
        d.setNotice(QStringLiteral("100"));
        QCOMPARE(d.core->storageLimits().ticker, 70);
        QCOMPARE(d.core->updatesLimits().ticker, 3);
        // Between, in a straight line.
        d.setNotice(QStringLiteral("25"));
        QCOMPARE(d.core->storageLimits().ticker, 80);
        QCOMPARE(d.core->storageLimits().island, 93);
        QCOMPARE(d.core->updatesLimits().ticker, 14);
        QCOMPARE(d.core->updatesLimits().island, 33);
        d.setNotice(QStringLiteral("75"));
        QCOMPARE(d.core->updatesLimits().island, 16);
        // Out of range holds at the end.
        d.setNotice(QStringLiteral("140"));
        QCOMPARE(d.core->notice(), 100);
        d.setNotice(QStringLiteral("-5"));
        QCOMPARE(d.core->notice(), 0);
        // A slider set to 80 is felt at the next look.
        d.setNotice(QStringLiteral("80"));
        d.look(73);
        QCOMPARE(d.topics, (QStringList{QStringLiteral("storage")}));
    }

    void updatesSwitchedOffAreSilentAndAbsent()
    {
        Desk d;
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Updates/Enabled"), false);
        }
        QVERIFY(!d.core->watchesUpdates());
        d.updates(40, 40);
        QVERIFY(d.fact(QStringLiteral("updates")).isEmpty());
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(!d.core->act(QStringLiteral("updates"), QStringLiteral("open")));
    }

    void healthKeepsOnlyItsOwnProblems()
    {
        const HealthLook look = readHealthFacts(QStringLiteral("problem\tservices\tServices\tOne didn't start.\n"
                                                               "problem\tspace\tSpace\t1 GB free of 477 GB.\n"
                                                               "look\tcrashes\tCrashes\t3 this week.\n"
                                                               "good\tdrive\tDrive\tIt reports no problems.\n"
                                                               "a line that isn't one\n"),
                                                Start);
        QCOMPARE(look.problems.size(), 1);
        QCOMPARE(look.problems.first().id, QStringLiteral("services"));
        const HealthFact fact = judge(look);
        QCOMPARE(fact.line, QStringLiteral("1 problem"));
        QCOMPARE(fact.detail, QStringLiteral("Services: One didn't start."));
        QCOMPARE(fact.level, Level::Ticker);
        QCOMPARE(judge(readHealthFacts(QString(), Start)).line, QStringLiteral("All good"));
    }

    void healthSpeaksOfANewProblemAndAFailingDriveGetsTheIsland()
    {
        Desk d;
        QVERIFY(d.core->healthDue());
        d.health({});
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(!d.core->healthDue());
        d.health({QStringLiteral("services")});
        QCOMPARE(d.ticker, QStringList{QStringLiteral("Health · 1 problem | Services: It went wrong.")});
        QCOMPARE(d.topics, QStringList{QStringLiteral("health")});
        // The same problem again is not news.
        d.health({QStringLiteral("services")});
        QCOMPARE(d.ticker.size(), 1);
        // The drive failing is the one that takes the island, and leads.
        d.health({QStringLiteral("services"), QStringLiteral("drive")});
        QCOMPARE(d.ticker.size(), 1);
        const QVariantMap fact = d.fact(QStringLiteral("health"));
        QCOMPARE(fact.value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QCOMPARE(fact.value(QStringLiteral("detail")).toString(), QStringLiteral("Drive: It went wrong."));
        QVERIFY(d.core->act(QStringLiteral("health"), QStringLiteral("open")));
        QCOMPARE(d.opened, QStringList{QStringLiteral("health")});
        QVERIFY(d.logbook().contains(QStringLiteral("Health  Showed the island: 2 problems")));
    }

    void healthLooksAgainAfterAWeekAndSurvivesARestart()
    {
        Desk d;
        d.health({QStringLiteral("packages")});
        d.make();
        QCOMPARE(d.fact(QStringLiteral("health")).value(QStringLiteral("line")).toString(), QStringLiteral("1 problem"));
        QVERIFY(!d.core->healthDue());
        d.now = d.now.addDays(HealthEveryDays);
        QVERIFY(d.core->healthDue());
        // Nothing new after the restart: the problem was already spoken of.
        d.health({QStringLiteral("packages")}, 0);
        QCOMPARE(d.ticker.size(), 1);
        // Fixed: settled, so a problem later speaks again.
        d.health({});
        QVERIFY(d.logbook().contains(QStringLiteral("Health  Settled at All good")));
        d.health({QStringLiteral("packages")});
        QCOMPARE(d.ticker.size(), 2);
    }

    void notNowQuietsHealthUntilAProblemMore()
    {
        Desk d;
        d.health({QStringLiteral("services")});
        QVERIFY(d.core->act(QStringLiteral("health"), QStringLiteral("not-now")));
        QVERIFY(d.logbook().contains(QStringLiteral("Health  You said not now; quiet until a problem more.")));
        d.health({QStringLiteral("services"), QStringLiteral("packages")});
        QCOMPARE(d.ticker.size(), 2);
    }

    void healthSwitchedOffIsSilentAndAbsent()
    {
        Desk d;
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Health/Enabled"), false);
        }
        QVERIFY(!d.core->healthDue());
        d.health({QStringLiteral("drive")});
        QVERIFY(d.fact(QStringLiteral("health")).isEmpty());
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(!d.core->act(QStringLiteral("health"), QStringLiteral("open")));
    }

    void aWindowsInstallerIsNamedForItsProgram()
    {
        QVERIFY(isWindowsInstaller(QStringLiteral("ZoomInstaller.exe")));
        QVERIFY(isWindowsInstaller(QStringLiteral("Setup.MSI")));
        QVERIFY(!isWindowsInstaller(QStringLiteral("zoom.deb")));
        QVERIFY(!isWindowsInstaller(QStringLiteral("ZoomInstaller.exe.part")));
        const QList<std::pair<const char *, const char *>> names{
            {"ZoomInstaller.exe", "Zoom"},
            {"ChromeSetup.exe", "Chrome"},
            {"SteamSetup.exe", "Steam"},
            {"Firefox Installer.exe", "Firefox"},
            {"vlc-3.0.21-win64.exe", "vlc"},
            {"Discord-Setup-x64 (1).exe", "Discord"},
            {"spotify_installer-1.2.45.exe", "spotify"},
            {"OBS-Studio-30.2-Full-Installer-x64.exe", "OBS Studio"},
            {"setup.exe", ""},
        };
        for (const auto &[file, name] : names)
            QCOMPARE(programName(QString::fromUtf8(file)), QString::fromUtf8(name));
    }

    void aWindowsInstallerGetsTheIslandOnceAndFindsTheLinuxVersion()
    {
        Desk d;
        QVERIFY(d.core->watchesDownloads());
        QVERIFY(d.fact(QStringLiteral("windows")).isEmpty());
        d.core->seeInstaller(QStringLiteral("ZoomInstaller.exe"));
        QVariantMap fact = d.fact(QStringLiteral("windows"));
        QCOMPARE(fact.value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QCOMPARE(fact.value(QStringLiteral("line")).toString(), QStringLiteral("ZoomInstaller.exe is for Windows"));
        QCOMPARE(fact.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("Windows programs don't run here. Find the Linux version of Zoom"));
        QCOMPARE(fact.value(QStringLiteral("actionText")).toString(), QStringLiteral("Find Zoom"));
        QVERIFY(d.ticker.isEmpty());
        // Another the same day: the island's allowance is spent, so the ticker.
        d.core->seeInstaller(QStringLiteral("ChromeSetup.exe"));
        QCOMPARE(d.ticker, QStringList{QStringLiteral("Windows programs · ChromeSetup.exe is for Windows | "
                                                      "Windows programs don't run here. Find the Linux version of Chrome")});
        QVERIFY(d.core->act(QStringLiteral("windows"), QStringLiteral("open")));
        QCOMPARE(d.opened, QStringList{QStringLiteral("install: Chrome")});
        // Once looked at, it's no longer news.
        QVERIFY(d.fact(QStringLiteral("windows")).isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Windows programs  You looked for the Linux version of ChromeSetup.exe.")));
        // Not a Windows installer: nothing.
        d.core->seeInstaller(QStringLiteral("notes.pdf"));
        QCOMPARE(d.ticker.size(), 1);
    }

    void toldNotToTellAgainItStopsForGood()
    {
        Desk d;
        d.core->seeInstaller(QStringLiteral("ZoomInstaller.exe"));
        QVERIFY(d.core->act(QStringLiteral("windows"), QStringLiteral("never")));
        QVERIFY(!d.core->watchesDownloads());
        QVERIFY(d.fact(QStringLiteral("windows")).isEmpty());
        d.make();
        QVERIFY(!d.core->watchesDownloads());
        d.now = d.now.addDays(2);
        d.core->seeInstaller(QStringLiteral("ChromeSetup.exe"));
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(d.fact(QStringLiteral("windows")).isEmpty());
    }

    void windowsProgramsSwitchedOffAreSilent()
    {
        Desk d;
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Windows/Enabled"), false);
        }
        QVERIFY(!d.core->watchesDownloads());
        d.core->seeInstaller(QStringLiteral("ZoomInstaller.exe"));
        QVERIFY(d.fact(QStringLiteral("windows")).isEmpty());
        QVERIFY(!d.core->act(QStringLiteral("windows"), QStringLiteral("open")));
    }

    void downloadsSpeakPastFiveGigabytesAndAgainFiveLater()
    {
        Desk d;
        QVERIFY(d.core->watchesTidy());
        d.core->seeTidy(4 * Gb);
        QVERIFY(d.ticker.isEmpty());
        d.core->seeTidy(qint64(7.4 * Gb));
        QCOMPARE(d.ticker, QStringList{QStringLiteral("Downloads · 7.4 GB | Tidy up Downloads")});
        QCOMPARE(d.topics, QStringList{QStringLiteral("tidy")});
        QVERIFY(d.logbook().contains(QStringLiteral("Tidy up  Told the ticker: Downloads and the Desktop hold 7.4 GB.")));
        // Growing, but not yet 5 GB more: quiet.
        d.core->seeTidy(11 * Gb);
        QCOMPARE(d.ticker.size(), 1);
        // Remembered through a restart.
        d.make();
        d.core->seeTidy(12 * Gb);
        QCOMPARE(d.ticker.size(), 1);
        d.core->seeTidy(qint64(12.5 * Gb));
        QCOMPARE(d.ticker.size(), 2);
        // Tidied to 2 GB: the next mark is 7 GB.
        d.core->seeTidy(2 * Gb);
        d.core->seeTidy(6 * Gb);
        QCOMPARE(d.ticker.size(), 2);
        // Do Not Disturb holds it until it is off.
        d.doNotDisturb = true;
        d.core->seeTidy(8 * Gb);
        QCOMPARE(d.ticker.size(), 2);
        d.doNotDisturb = false;
        d.core->seeTidy(8 * Gb);
        QCOMPARE(d.ticker.size(), 3);
        // A tap opens the card.
        QVERIFY(d.core->act(QStringLiteral("tidy"), QStringLiteral("open")));
        QCOMPARE(d.opened, QStringList{QStringLiteral("tidy")});
        QVERIFY(d.logbook().contains(QStringLiteral("Tidy up  You opened Tidy up Downloads.")));
        // Switched off: silent, and a tap opens nothing.
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Tidy/Enabled"), false);
        }
        QVERIFY(!d.core->watchesTidy());
        d.core->seeTidy(40 * Gb);
        QCOMPARE(d.ticker.size(), 3);
        QVERIFY(!d.core->act(QStringLiteral("tidy"), QStringLiteral("open")));
    }

    void theHelpersAnswerToWindowsWords()
    {
        const auto findsBy = [](const QString &words) {
            for (const QVariantMap &action : Core::actions()) {
                if (action.value(QStringLiteral("keywords")).toStringList().contains(words))
                    return action.value(QStringLiteral("id")).toString();
            }
            return QString();
        };
        QCOMPARE(findsBy(QStringLiteral("add or remove programs")), QStringLiteral("install"));
        QCOMPARE(findsBy(QStringLiteral("disk cleanup")), QStringLiteral("space"));
        QCOMPARE(findsBy(QStringLiteral("windows update")), QStringLiteral("update"));
        QCOMPARE(findsBy(QStringLiteral("system restore")), QStringLiteral("restore"));
    }

    void theBusOffersTheFacts()
    {
        // The test's own bus connection to itself, where there is a bus.
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected())
            QSKIP("No session bus for this test.");
        Desk d;
        d.look(92);
        CoreService service(d.core.get());
        QVERIFY(service.publish(bus));
        QVERIFY(bus.registerService(QLatin1String(CoreService::Service)));
        qDBusRegisterMetaType<QList<QVariantMap>>();
        const QDBusReply<QList<QVariantMap>> facts
            = bus.call(QDBusMessage::createMethodCall(QLatin1String(CoreService::Service), QLatin1String(CoreService::Path),
                                                      QStringLiteral("io.github.carlsonjm.SplitRock.Core"), QStringLiteral("Facts")));
        QVERIFY2(facts.isValid(), qPrintable(facts.error().message()));
        QCOMPARE(facts.value().size(), 1);
        QCOMPARE(facts.value().first().value(QStringLiteral("line")).toString(), QStringLiteral("92% full"));
        const QDBusReply<QList<QVariantMap>> actions
            = bus.call(QDBusMessage::createMethodCall(QLatin1String(CoreService::Service), QLatin1String(CoreService::Path),
                                                      QStringLiteral("io.github.carlsonjm.SplitRock.Core"), QStringLiteral("Actions")));
        QVERIFY2(actions.isValid(), qPrintable(actions.error().message()));
        QCOMPARE(actions.value().size(), 9);
        bus.unregisterService(QLatin1String(CoreService::Service));
        bus.unregisterObject(QLatin1String(CoreService::Path));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
