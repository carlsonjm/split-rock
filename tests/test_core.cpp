// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core, the front desk: what it makes of its looks at the drive, when it
// speaks and where, the files it keeps, and its bus interface. Every look and
// every clock here is the test's own; nothing looks at this computer's drive,
// and files are made only inside the test's own home.
#include "core/Attention.h"
#include "core/Core.h"
#include "core/CoreService.h"
#include "core/Storage.h"

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
    QStringList opened;
    std::unique_ptr<Core> core;

    Desk() { make(); }

    void make()
    {
        Core::Hooks hooks;
        hooks.look = [this](const QDateTime &when) { return at(when, percent); };
        hooks.now = [this] { return now; };
        hooks.doNotDisturb = [this] { return doNotDisturb; };
        hooks.notify = [this](const QString &summary, const QString &body) { ticker.append(summary + QStringLiteral(" | ") + body); };
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

    QVariantMap fact() const { return core->facts().value(0); }
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
        QCOMPARE(ids, (QStringList{QStringLiteral("update"), QStringLiteral("space"), QStringLiteral("install"),
                                   QStringLiteral("health"), QStringLiteral("changes"), QStringLiteral("restore"),
                                   QStringLiteral("community")}));
        QVERIFY(d.core->openAction(QStringLiteral("health"), QStringLiteral("health")));
        // Only Install an app takes what was typed.
        QVERIFY(d.core->openAction(QStringLiteral("install"), QStringLiteral(" install pdf editor ")));
        QVERIFY(!d.core->openAction(QStringLiteral("format-the-disk"), QString()));
        QCOMPARE(d.opened, (QStringList{QStringLiteral("health"), QStringLiteral("install: install pdf editor")}));
        QVERIFY(d.logbook().contains(QStringLiteral("Search  You opened Health check.")));
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
        QCOMPARE(actions.value().size(), 7);
        bus.unregisterService(QLatin1String(CoreService::Service));
        bus.unregisterObject(QLatin1String(CoreService::Path));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
