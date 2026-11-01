// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core, the front desk: what it makes of its looks at the drive, when it
// speaks and where, the files it keeps, and its bus interface. Every look and
// every clock here is the test's own; nothing looks at this computer's drive,
// and files are made only inside the test's own home.
#include "core/Attention.h"
#include "core/Automations.h"
#include "core/Core.h"
#include "core/CoreService.h"
#include "core/Devices.h"
#include "core/Health.h"
#include "core/Layouts.h"
#include "core/Storage.h"
#include "core/Updates.h"
#include "core/Windows.h"
#include "core/WorkspaceMemory.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QFile>
#include <QStandardPaths>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QUuid>

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
    QStringList settingsOpened;
    int notificationsOpened = 0;
    int bringBacks = 0;
    int dismissed = 0;
    QList<QStringList> appsOpened;
    QString signIn = QStringLiteral("1");
    QStringList settingsInstalled{QStringLiteral("kcm_printer_manager"), QStringLiteral("kcm_device_automounter"),
                                  QStringLiteral("kcm_bluetooth")};
    // Saved layouts: what Cards reads of the desk, what it accepts, each
    // arrangement asked for, and the run under way, which the test ends.
    std::optional<CurrentDesk> cards;
    LayoutPlan plan{true, {}, {}, {}};
    QList<QList<LayoutGroup>> arranged;
    std::function<void(const LayoutRun &)> runDone;
    // Anything the test wires differently, such as Cards on the bus.
    std::function<void(Core::Hooks &)> wire;
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
        hooks.hasSettings = [this](const QString &page) { return settingsInstalled.contains(page); };
        hooks.openSettings = [this](const QString &page) { settingsOpened.append(page); };
        hooks.openNotifications = [this] { ++notificationsOpened; };
        hooks.bringBack = [this] { ++bringBacks; };
        hooks.dismissSavedSession = [this] { ++dismissed; };
        hooks.openApps = [this](const QStringList &ids) { appsOpened.append(ids); };
        hooks.signIn = [this] { return signIn; };
        hooks.appName = [](const QString &id) {
            return id == QLatin1String("org.mozilla.firefox") ? QStringLiteral("Firefox") : appName(id);
        };
        hooks.currentLayout = [this](std::function<void(std::optional<CurrentDesk>)> answered) { answered(cards); };
        hooks.arrangeLayout = [this](const QList<LayoutGroup> &groups, std::function<void(const LayoutPlan &)> armed,
                                     std::function<void(const LayoutRun &)> done) {
            arranged.append(groups);
            if (!plan.accepted) {
                LayoutRun refused;
                refused.reason = plan.reason;
                refused.finished = true;
                done(refused);
                return;
            }
            runDone = std::move(done);
            armed(plan);
        };
        if (wire)
            wire(hooks);
        core = std::make_unique<Core>(dir.filePath(QStringLiteral("core")), dir.filePath(QStringLiteral("corerc")), hooks);
    }

    // Cards says the run under way is over.
    void endRun(int placed, const QStringList &missedApps = {})
    {
        LayoutRun run;
        run.accepted = true;
        run.finished = true;
        run.launch = plan.launch;
        run.alreadyOpen = plan.alreadyOpen;
        run.placed = placed;
        run.missed = int(missedApps.size());
        run.missedApps = missedApps;
        const auto done = std::move(runDone);
        runDone = nullptr;
        done(run);
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
        settings.setValue(QStringLiteral("Notice"), notice);
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

namespace {

// A session Cards offers, saved this many hours before the test's clock.
SavedSession session(const QDateTime &now, int hoursAgo, const QStringList &apps)
{
    SavedSession s;
    s.available = true;
    s.count = int(apps.size());
    s.apps = apps;
    s.when = now.addSecs(-hoursAgo * 3600);
    return s;
}

OpenSet together(const QStringList &apps, int count, const QDateTime &lastSeen)
{
    OpenSet set;
    set.apps = apps;
    set.apps.sort();
    set.count = count;
    set.lastSeen = lastSeen;
    return set;
}

const QStringList ThreeApps{QStringLiteral("org.mozilla.firefox"), QStringLiteral("org.kde.notes"), QStringLiteral("mail")};

const QStringList FourApps{QStringLiteral("org.mozilla.firefox"), QStringLiteral("org.kde.kate"),
                           QStringLiteral("spotify"), QStringLiteral("org.kde.dolphin")};

} // namespace

// Kadunce's Workspace Memory calls, as the contract gives them, on a
// connection of their own on the test's bus.
class StandInKadunce : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "co.goodinput.Kadunce")

public:
    int version = 1;
    QVariantMap saved;
    QVariantMap plan;
    QVariantList together;
    QStringList calls;
    QString token;

    void finish(int placed, int missed)
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/Kadunce"), QStringLiteral("co.goodinput.Kadunce"),
                                                         QStringLiteral("bringBackFinished"));
        signal.setArguments({token, placed, missed});
        QDBusConnection(QStringLiteral("kadunce")).send(signal);
    }

public Q_SLOTS:
    int workspaceMemoryProtocolVersion()
    {
        calls.append(QStringLiteral("version"));
        return version;
    }
    QVariantMap savedSession()
    {
        calls.append(QStringLiteral("savedSession"));
        return saved;
    }
    QVariantMap bringBack(const QString &t)
    {
        calls.append(QStringLiteral("bringBack"));
        token = t;
        return plan;
    }
    void cancelBringBack(const QString &) { calls.append(QStringLiteral("cancelBringBack")); }
    void dismissSavedSession() { calls.append(QStringLiteral("dismissSavedSession")); }
    QVariantList openTogether()
    {
        calls.append(QStringLiteral("openTogether"));
        return together;
    }

    // Kadunce's layout calls, as the saved layouts contract gives them.
public:
    QVariantMap desk;
    QVariantMap layoutPlan;
    QList<QVariantMap> arrangedGroups;
    QString layoutToken;

    void placed(const QString &appId, bool went)
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/Kadunce"), QStringLiteral("co.goodinput.Kadunce"),
                                                         QStringLiteral("layoutAppPlaced"));
        signal.setArguments({layoutToken, appId, went});
        QDBusConnection(QStringLiteral("kadunce")).send(signal);
    }
    void layoutDone(int placedCount, int missed)
    {
        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/Kadunce"), QStringLiteral("co.goodinput.Kadunce"),
                                                         QStringLiteral("layoutFinished"));
        signal.setArguments({layoutToken, placedCount, missed});
        QDBusConnection(QStringLiteral("kadunce")).send(signal);
    }

public Q_SLOTS:
    int layoutProtocolVersion()
    {
        calls.append(QStringLiteral("layoutVersion"));
        return version;
    }
    QVariantMap currentLayout()
    {
        calls.append(QStringLiteral("currentLayout"));
        return desk;
    }
    QVariantMap arrangeLayout(const QString &t, const QList<QVariantMap> &groups)
    {
        calls.append(QStringLiteral("arrangeLayout"));
        layoutToken = t;
        arrangedGroups = groups;
        return layoutPlan;
    }
    void cancelLayout(const QString &) { calls.append(QStringLiteral("cancelLayout")); }
};

class TestCore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // Core's look and the Check for updates Helper fetch the package lists
    // into one folder, which outlives a restart.
    void updatesShareOneListsFolder()
    {
        QCOMPARE(updatesListsFolder(),
                 QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/split-rock/checkup-db"));
        QFile script(QStringLiteral(PROJECT_DIR "/src/app/SystemUpdate.cpp"));
        QVERIFY(script.open(QIODevice::ReadOnly));
        QVERIFY(script.readAll().contains(R"(CHECKUPDATES_DB="${XDG_CACHE_HOME:-$HOME/.cache}/split-rock/checkup-db")"));
    }

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

    // Shuffle's Settings writes the switch for all of Core and the Pings
    // slider into a [General] group, as KConfig does.
    void theGeneralGroupSettingsWritesIsRead()
    {
        Desk d;
        {
            QFile file(d.dir.filePath(QStringLiteral("corerc")));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[General]\nEnabled=false\n");
        }
        d.look(95);
        QVERIFY(d.core->facts().isEmpty());
        QVERIFY(d.ticker.isEmpty());
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
        // The Helpers row's order: six in the first row, the rest behind More.
        QCOMPARE(ids, (QStringList{QStringLiteral("install"), QStringLiteral("apps"), QStringLiteral("update"), QStringLiteral("space"),
                                   QStringLiteral("health"), QStringLiteral("changes"), QStringLiteral("tidy"), QStringLiteral("restore"),
                                   QStringLiteral("community")}));
        QStringList more;
        for (const QVariantMap &action : Core::actions()) {
            if (action.value(QStringLiteral("more")).toBool())
                more.append(action.value(QStringLiteral("id")).toString());
        }
        QCOMPARE(more, (QStringList{QStringLiteral("tidy"), QStringLiteral("restore"), QStringLiteral("community")}));
        // Without an app store, Add the app store leads and What changed
        // goes behind More, so the first row stays six.
        const QList<QVariantMap> missing = Core::actions(true);
        QCOMPARE(missing.first().value(QStringLiteral("id")).toString(), QStringLiteral("appstore"));
        int first = 0;
        for (const QVariantMap &action : missing)
            first += action.value(QStringLiteral("more")).toBool() ? 0 : 1;
        QCOMPARE(first, 6);
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

    void aWornBatterySpeaksOnceAtTheTickerNeverTheIsland()
    {
        Desk d;
        d.health({});
        d.now = d.now.addDays(HealthEveryDays);
        const QString worn = QStringLiteral("problem\tbattery\tBattery\tHolds 64% of its original charge. It runs out "
                                            "sooner than it did new.\n");
        d.core->seeHealth(readHealthFacts(worn, d.now));
        QCOMPARE(d.ticker.size(), 1);
        QVERIFY(d.ticker.first().startsWith(QStringLiteral("Health · 1 problem | Battery: Holds 64%")));
        QCOMPARE(d.fact(QStringLiteral("health")).value(QStringLiteral("attention")).toString(), QStringLiteral("none"));
        // The next week's look, still worn, is not news.
        d.now = d.now.addDays(HealthEveryDays);
        d.core->seeHealth(readHealthFacts(worn, d.now));
        QCOMPARE(d.ticker.size(), 1);
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

    void theAppStoreIsOfferedOnlyWhereMissing()
    {
        Desk d;
        bool missing = true;
        d.wire = [&missing](Core::Hooks &hooks) { hooks.appStoreMissing = [&missing] { return missing; }; };
        d.make();
        QSignalSpy changed(d.core.get(), &Core::actionsChanged);
        QCOMPARE(d.core->offeredActions().first().value(QStringLiteral("id")).toString(), QStringLiteral("install"));
        d.core->lookAtAppStore();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(d.core->offeredActions().first().value(QStringLiteral("id")).toString(), QStringLiteral("appstore"));
        // Looking again with nothing changed says nothing.
        d.core->lookAtAppStore();
        QCOMPARE(changed.size(), 1);
        QVERIFY(d.core->openAction(QStringLiteral("appstore"), QStringLiteral("anything")));
        QCOMPARE(d.opened.last(), QStringLiteral("appstore"));
        missing = false;
        d.core->check();
        QCOMPARE(changed.size(), 2);
        QCOMPARE(d.core->offeredActions().first().value(QStringLiteral("id")).toString(), QStringLiteral("install"));
        // Its card still opens, to say the computer has one.
        QVERIFY(d.core->openAction(QStringLiteral("appstore"), QString()));
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

    void devicesAreReadFromWhatTheBusSays()
    {
        // UDisks2: only a drive that can be unplugged.
        const auto stick = driveFrom({{QStringLiteral("Id"), QStringLiteral("SanDisk-Cruzer-Blade-4C53")},
                                      {QStringLiteral("Vendor"), QStringLiteral("SanDisk")},
                                      {QStringLiteral("Model"), QStringLiteral("Cruzer_Blade  ")},
                                      {QStringLiteral("Removable"), true}});
        QVERIFY(stick);
        QCOMPARE(stick->id, QStringLiteral("SanDisk-Cruzer-Blade-4C53"));
        QCOMPARE(stick->name, QStringLiteral("SanDisk Cruzer Blade"));
        QVERIFY(!driveFrom({{QStringLiteral("Id"), QStringLiteral("Samsung-SSD-990")}, {QStringLiteral("Removable"), false}}));
        QCOMPARE(driveFrom({{QStringLiteral("Id"), QStringLiteral("x")}, {QStringLiteral("Removable"), true}})->name,
                 QStringLiteral("A drive"));
        // BlueZ: only once paired, by its own name.
        const QVariantMap headphones{{QStringLiteral("Address"), QStringLiteral("AC:80:0A:12:34:56")},
                                     {QStringLiteral("Alias"), QStringLiteral("WH-1000XM4")},
                                     {QStringLiteral("Paired"), true}};
        QCOMPARE(bluetoothFrom(headphones)->name, QStringLiteral("WH-1000XM4"));
        QCOMPARE(bluetoothFrom(headphones)->id, QStringLiteral("AC:80:0A:12:34:56"));
        QVariantMap unpaired = headphones;
        unpaired.insert(QStringLiteral("Paired"), false);
        QVERIFY(!bluetoothFrom(unpaired));
        // A printer by the model systemd describes it with.
        QCOMPARE(printerFrom(QStringLiteral("LaserJet_M110we"))->name, QStringLiteral("LaserJet M110we"));
        QVERIFY(!printerFrom(QStringLiteral("  ")));
        QCOMPARE(deviceSettingsPage(DeviceKind::Bluetooth), QStringLiteral("kcm_bluetooth"));
    }

    void aNewDeviceSpeaksOnceAtTheTickerAndOpensItsSettings()
    {
        Desk d;
        QVERIFY(d.core->watchesDevices());
        const Device stick{DeviceKind::Drive, QStringLiteral("SanDisk-1"), QStringLiteral("SanDisk Cruzer Blade")};
        const Device disk{DeviceKind::Drive, QStringLiteral("WD-2"), QStringLiteral("WD Elements")};
        const Device printer{DeviceKind::Printer, QStringLiteral("LaserJet M110we"), QStringLiteral("LaserJet M110we")};
        // What is there when Core first looks is known already.
        d.core->seeDevices(DeviceKind::Drive, {stick});
        d.core->seeDevices(DeviceKind::Printer, {});
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Devices  Looked at the drives here: 1 known.")));
        // One plugged in later is news once, at the ticker, during Do Not Disturb too.
        d.doNotDisturb = true;
        d.core->seeDevices(DeviceKind::Drive, {stick, disk});
        QCOMPARE(d.ticker, QStringList{QStringLiteral("New drive · Set it up | WD Elements")});
        QCOMPARE(d.topics, QStringList{QStringLiteral("devices")});
        QVERIFY(d.logbook().contains(QStringLiteral("Devices  New drive: WD Elements.")));
        d.core->seeDevices(DeviceKind::Drive, {stick});
        d.core->seeDevices(DeviceKind::Drive, {stick, disk});
        QCOMPARE(d.ticker.size(), 1);
        // It never takes the island, and has no fact.
        for (const QVariantMap &fact : d.core->facts())
            QVERIFY(fact.value(QStringLiteral("topic")).toString() != QStringLiteral("devices"));
        // A tap opens the settings for the kind it spoke of last.
        d.core->seeDevices(DeviceKind::Printer, {printer});
        QCOMPARE(d.ticker.last(), QStringLiteral("New printer · Set it up | LaserJet M110we"));
        QVERIFY(d.core->act(QStringLiteral("devices"), QStringLiteral("open")));
        QCOMPARE(d.settingsOpened, QStringList{QStringLiteral("kcm_printer_manager")});
        QVERIFY(d.logbook().contains(QStringLiteral("Devices  You opened the printer settings.")));
        QVERIFY(!d.core->act(QStringLiteral("devices"), QStringLiteral("not-now")));
        // Known across a restart.
        d.make();
        d.core->seeDevices(DeviceKind::Printer, {printer});
        d.core->seeDevices(DeviceKind::Drive, {stick, disk});
        QCOMPARE(d.ticker.size(), 2);
        QVERIFY(d.core->act(QStringLiteral("devices"), QStringLiteral("open")));
        // A kind whose settings page isn't installed is kept in the logbook alone.
        d.settingsInstalled.removeAll(QStringLiteral("kcm_bluetooth"));
        d.core->seeDevices(DeviceKind::Bluetooth, {});
        d.core->seeDevices(DeviceKind::Bluetooth, {{DeviceKind::Bluetooth, QStringLiteral("AC:80"), QStringLiteral("WH-1000XM4")}});
        QCOMPARE(d.ticker.size(), 2);
        QVERIFY(d.logbook().contains(QStringLiteral("Devices  New Bluetooth device: WH-1000XM4.")));
    }

    void controllersAndScreensAreReadFromTheKernelsFolders()
    {
        QTemporaryDir sys;
        const auto put = [&sys](const QString &path, const QByteArray &bytes) {
            QDir().mkpath(QFileInfo(sys.filePath(path)).absolutePath());
            QFile file(sys.filePath(path));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(bytes);
        };
        // A game pad, and a keyboard's event node beside it that is no
        // controller.
        put(QStringLiteral("input/js0/device/name"), "Xbox Wireless Controller\n");
        put(QStringLiteral("input/js0/device/id/vendor"), "045e\n");
        put(QStringLiteral("input/js0/device/id/product"), "0b13\n");
        put(QStringLiteral("input/js0/device/uniq"), "98:7a:14:00:00:01\n");
        put(QStringLiteral("input/event3/device/name"), "AT Translated Set 2 keyboard\n");
        const QList<Device> pads = controllersIn(sys.filePath(QStringLiteral("input")));
        QCOMPARE(pads.size(), 1);
        QCOMPARE(pads.first().kind, DeviceKind::Controller);
        QCOMPARE(pads.first().name, QStringLiteral("Xbox Wireless Controller"));
        QCOMPARE(pads.first().id, QStringLiteral("045e:0b13:98:7a:14:00:00:01"));

        // A screen's EDID: maker DEL, model 0xa0f1, its name in a 0xFC
        // descriptor.
        QByteArray edid(128, '\0');
        const QByteArray header("\x00\xff\xff\xff\xff\xff\xff\x00", 8);
        edid.replace(0, 8, header);
        const quint16 maker = ('D' - 'A' + 1) << 10 | ('E' - 'A' + 1) << 5 | ('L' - 'A' + 1);
        edid[8] = char(maker >> 8);
        edid[9] = char(maker & 0xff);
        edid[10] = char(0xf1);
        edid[11] = char(0xa0);
        edid[12] = 0x2a;
        const QByteArray named = QByteArray("\x00\x00\x00\xfc\x00", 5) + QByteArray("DELL U2723QE\n");
        edid.replace(72, named.size(), named);
        const auto dell = screenFrom(edid, QStringLiteral("DP-1"));
        QVERIFY(dell);
        QCOMPARE(dell->name, QStringLiteral("DELL U2723QE"));
        QCOMPARE(dell->id, QStringLiteral("DEL-a0f1-42"));
        QVERIFY(!screenFrom(QByteArray(20, '\0'), QStringLiteral("DP-1")));

        put(QStringLiteral("drm/card1-eDP-1/status"), "connected\n");
        put(QStringLiteral("drm/card1-eDP-1/edid"), QByteArray(edid).replace(12, 1, "\x01"));
        put(QStringLiteral("drm/card1-DP-1/status"), "connected\n");
        put(QStringLiteral("drm/card1-DP-1/edid"), edid);
        put(QStringLiteral("drm/card1-HDMI-A-1/status"), "disconnected\n");
        put(QStringLiteral("drm/card1-HDMI-A-1/edid"), QByteArray());
        const QList<Device> screens = screensIn(sys.filePath(QStringLiteral("drm")));
        QCOMPARE(screens.size(), 2);

        // Through Core: what is there at first is known; a screen plugged in
        // later is news, and opens Display Configuration.
        Desk d;
        d.settingsInstalled << QStringLiteral("kcm_kscreen") << QStringLiteral("kcm_gamecontroller");
        d.core->seeDevices(DeviceKind::Screen, {screens.first()});
        d.core->seeDevices(DeviceKind::Controller, {});
        QVERIFY(d.logbook().contains(QStringLiteral("Devices  Looked at the screens here: 1 known.")));
        QVERIFY(d.logbook().contains(QStringLiteral("Devices  Looked at the game controllers here: 0 known.")));
        d.core->seeDevices(DeviceKind::Screen, screens);
        QCOMPARE(d.ticker, QStringList{QStringLiteral("New screen · Set it up | DELL U2723QE")});
        QVERIFY(d.core->act(QStringLiteral("devices"), QStringLiteral("open")));
        QCOMPARE(d.settingsOpened.last(), QStringLiteral("kcm_kscreen"));
        d.core->seeDevices(DeviceKind::Controller, pads);
        QCOMPARE(d.ticker.last(), QStringLiteral("New game controller · Set it up | Xbox Wireless Controller"));
        // Known across a restart, and the tap still opens the right page.
        d.make();
        d.core->seeDevices(DeviceKind::Controller, pads);
        d.core->seeDevices(DeviceKind::Screen, screens);
        QCOMPARE(d.ticker.size(), 2);
        QVERIFY(d.core->act(QStringLiteral("devices"), QStringLiteral("open")));
        QCOMPARE(d.settingsOpened.last(), QStringLiteral("kcm_gamecontroller"));
        QVERIFY(d.logbook().contains(QStringLiteral("Devices  You opened the game controller settings.")));
    }

    void devicesSwitchedOffAreSilent()
    {
        Desk d;
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Devices/Enabled"), false);
        }
        QVERIFY(!d.core->watchesDevices());
        d.core->seeDevices(DeviceKind::Drive, {});
        d.core->seeDevices(DeviceKind::Drive, {{DeviceKind::Drive, QStringLiteral("WD-2"), QStringLiteral("WD Elements")}});
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(!d.core->act(QStringLiteral("devices"), QStringLiteral("open")));
    }

    void workspaceMemoryAsksOnceOnTheIsland()
    {
        Desk d;
        QVERIFY(d.core->watchesMemory());
        QVERIFY(!d.core->bringsBackAlways());
        // Nothing saved, or a session with no apps: nothing to say.
        d.core->seeSavedSession(SavedSession());
        QVERIFY(d.core->facts().isEmpty());
        d.core->seeSavedSession(session(d.now, 14, FourApps));
        const QVariantMap fact = d.fact(QStringLiteral("memory"));
        QCOMPARE(fact.value(QStringLiteral("line")).toString(), QStringLiteral("Bring back 4 apps from yesterday?"));
        QCOMPARE(fact.value(QStringLiteral("detail")).toString(), QStringLiteral("Each one goes back to its place"));
        QCOMPARE(fact.value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QCOMPARE(fact.value(QStringLiteral("actionText")).toString(), QStringLiteral("Bring back"));
        QCOMPARE(fact.value(QStringLiteral("apps")).toStringList().first(), QStringLiteral("Firefox"));
        QCOMPARE(fact.value(QStringLiteral("choices")).value<QList<QVariantMap>>().first().value(QStringLiteral("action")).toString(),
                 QStringLiteral("always"));
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Workspace Memory  Showed the island: Bring back 4 apps from yesterday?")));
        // The same session again, as after Core restarts, is not news.
        d.make();
        d.core->seeSavedSession(session(d.now, 14, FourApps));
        QCOMPARE(d.fact(QStringLiteral("memory")).value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QVERIFY(d.ticker.isEmpty());

        // Bring back: Cards is asked, once, and the island goes.
        QVERIFY(d.core->act(QStringLiteral("memory"), QStringLiteral("open")));
        QCOMPARE(d.bringBacks, 1);
        QVERIFY(d.fact(QStringLiteral("memory")).isEmpty());
        QVERIFY(!d.core->act(QStringLiteral("memory"), QStringLiteral("open")));
        QCOMPARE(d.bringBacks, 1);
        BringBackRun run;
        run.accepted = true;
        run.finished = true;
        run.launched = FourApps;
        run.placed = 4;
        d.core->broughtBack(run);
        // Asked, so no ticker afterwards.
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Workspace Memory  Brought back 4 apps; 4 in place, 0 missed.")));
        QVERIFY(!d.core->bringsBackAlways());
    }

    // The desktop's own restore reopened the apps: only the ones still
    // missing are offered, and none missing is no offer at all.
    void onlyTheAppsNotOpenAreOffered()
    {
        Desk d;
        const QVariantMap answer{{QStringLiteral("available"), true},
                                 {QStringLiteral("count"), 4},
                                 {QStringLiteral("apps"), FourApps},
                                 {QStringLiteral("missing"), QStringList{FourApps.at(1)}},
                                 {QStringLiteral("when"), qlonglong(d.now.addSecs(-2 * 3600).toSecsSinceEpoch())}};
        const SavedSession some = savedSessionFrom(answer);
        QCOMPARE(some.count, 1);
        QCOMPARE(some.apps, QStringList{FourApps.at(1)});
        d.core->seeSavedSession(some);
        QCOMPARE(d.fact(QStringLiteral("memory")).value(QStringLiteral("line")).toString(),
                 QStringLiteral("Bring back 1 app from earlier today?"));

        Desk e;
        QVariantMap open = answer;
        open.insert(QStringLiteral("missing"), QStringList{});
        const SavedSession none = savedSessionFrom(open);
        QVERIFY(none.allOpen);
        QVERIFY(!none.available);
        e.core->seeSavedSession(none);
        QVERIFY(e.fact(QStringLiteral("memory")).isEmpty());
        QVERIFY(e.ticker.isEmpty());
        QCOMPARE(e.dismissed, 1);
        QVERIFY(e.logbook().contains(QStringLiteral("Workspace Memory  Every app Cards remembers is open already; nothing to bring back.")));

        // An older Cards that names nothing missing is offered as before.
        QVariantMap older = answer;
        older.remove(QStringLiteral("missing"));
        QCOMPARE(savedSessionFrom(older).count, 4);
    }

    void notNowLeavesTheDesktopAndTellsCards()
    {
        Desk d;
        d.core->seeSavedSession(session(d.now, 2, FourApps.mid(0, 1)));
        QCOMPARE(d.fact(QStringLiteral("memory")).value(QStringLiteral("line")).toString(),
                 QStringLiteral("Bring back 1 app from earlier today?"));
        QVERIFY(d.core->act(QStringLiteral("memory"), QStringLiteral("not-now")));
        QCOMPARE(d.dismissed, 1);
        QCOMPARE(d.bringBacks, 0);
        QVERIFY(d.fact(QStringLiteral("memory")).isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Workspace Memory  You said not now; the desktop stays as it is.")));
        QVERIFY(!d.core->act(QStringLiteral("memory"), QStringLiteral("never")));

        // The next sign-in's session is news again; a second the same day
        // goes to the ticker, as does one during Do Not Disturb.
        d.now = d.now.addSecs(3600);
        d.core->seeSavedSession(session(d.now, 1, FourApps));
        QVERIFY(d.fact(QStringLiteral("memory")).isEmpty());
        QCOMPARE(d.ticker, QStringList{QStringLiteral("Bring back 4 apps from earlier today? | Each one goes back to its place")});
        QCOMPARE(d.topics, QStringList{QStringLiteral("memory")});
        // The ticker's tap brings them back.
        QVERIFY(d.core->act(QStringLiteral("memory"), QStringLiteral("open")));
        QCOMPARE(d.bringBacks, 1);
        d.now = d.now.addDays(1);
        d.doNotDisturb = true;
        d.core->seeSavedSession(session(d.now, 20, FourApps));
        QCOMPARE(d.ticker.size(), 2);
        // A session from weeks ago is not offered.
        d.doNotDisturb = false;
        d.now = d.now.addDays(1);
        d.core->seeSavedSession(session(d.now, 24 * 30, FourApps));
        QCOMPARE(d.ticker.size(), 2);
        QVERIFY(d.logbook().contains(QStringLiteral("not offered")));
    }

    void alwaysBringsThemBackWithNoIsland()
    {
        Desk d;
        d.core->seeSavedSession(session(d.now, 30, FourApps));
        QVERIFY(d.fact(QStringLiteral("memory")).value(QStringLiteral("line")).toString().startsWith(QStringLiteral("Bring back 4 apps from ")));
        QVERIFY(d.core->act(QStringLiteral("memory"), QStringLiteral("always")));
        QCOMPARE(d.bringBacks, 1);
        QVERIFY(d.core->bringsBackAlways());
        QCOMPARE(QSettings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat)
                     .value(QStringLiteral("WorkspaceMemory/Always")).toBool(),
                 true);
        QVERIFY(d.fact(QStringLiteral("memory")).isEmpty());

        // The next sign-in: no island, Cards is asked straight away, and the
        // ticker says so once it is done.
        d.now = d.now.addDays(1);
        d.core->seeSavedSession(session(d.now, 12, FourApps));
        QCOMPARE(d.bringBacks, 2);
        QVERIFY(d.fact(QStringLiteral("memory")).isEmpty());
        QVERIFY(d.ticker.isEmpty());
        BringBackRun run;
        run.accepted = true;
        run.finished = true;
        run.launched = FourApps;
        run.placed = 4;
        d.core->broughtBack(run);
        QCOMPARE(d.ticker, QStringList{QStringLiteral("Brought back 4 apps | As you left them")});
        QVERIFY(d.logbook().contains(QStringLiteral("as you chose Always")));
        // One that didn't open in time is said.
        d.now = d.now.addDays(1);
        d.core->seeSavedSession(session(d.now, 12, FourApps));
        run.placed = 3;
        run.missed = 1;
        d.core->broughtBack(run);
        QCOMPARE(d.ticker.last(), QStringLiteral("Brought back 3 apps | 1 didn't open in time"));
        // Settings turns Always off: the island asks again.
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("WorkspaceMemory/Always"), false);
        }
        d.now = d.now.addDays(1);
        d.core->seeSavedSession(session(d.now, 12, FourApps));
        QCOMPARE(d.bringBacks, 3);
        QCOMPARE(d.fact(QStringLiteral("memory")).value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
    }

    void workspaceMemorySwitchedOffIsSilent()
    {
        Desk d;
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("WorkspaceMemory/Enabled"), false);
            settings.setValue(QStringLiteral("WorkspaceMemory/Always"), true);
        }
        QVERIFY(!d.core->watchesMemory());
        d.core->seeSavedSession(session(d.now, 12, FourApps));
        QVERIFY(d.core->facts().isEmpty());
        QCOMPARE(d.bringBacks, 0);
        QVERIFY(!d.core->act(QStringLiteral("memory"), QStringLiteral("open")));
    }

    void wordsForWhenAndWhich()
    {
        const QDateTime thursday(QDate(2026, 10, 29), QTime(9, 0));
        QCOMPARE(fromWhen(thursday.addSecs(-3600), thursday), QStringLiteral("from earlier today"));
        QCOMPARE(fromWhen(thursday.addDays(-1), thursday), QStringLiteral("from yesterday"));
        QCOMPARE(fromWhen(thursday.addDays(-3), thursday), QStringLiteral("from Monday"));
        QCOMPARE(fromWhen(thursday.addDays(-10), thursday), QStringLiteral("from 19 October"));
        QCOMPARE(namesList({QStringLiteral("Firefox")}), QStringLiteral("Firefox"));
        QCOMPARE(namesList({QStringLiteral("Firefox"), QStringLiteral("Notes")}), QStringLiteral("Firefox and Notes"));
        QCOMPARE(namesList({QStringLiteral("Firefox"), QStringLiteral("Notes"), QStringLiteral("Mail")}),
                 QStringLiteral("Firefox, Notes and Mail"));
        QCOMPARE(namesList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("E")}),
                 QStringLiteral("A, B, C and 2 more"));
        QCOMPARE(appName(QStringLiteral("org.kde.dolphin")), QStringLiteral("Dolphin"));
    }

    void kadunceIsAskedOnTheBusAndCoreLaunchesWhatItNames()
    {
        QDBusConnection standIn = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("kadunce"));
        QVERIFY(standIn.isConnected());
        StandInKadunce kadunce;
        const QDateTime saved = QDateTime::currentDateTimeUtc().addSecs(-3600 * 14);
        kadunce.saved = {{QStringLiteral("available"), true}, {QStringLiteral("count"), 3},
                         {QStringLiteral("apps"), QStringList{QStringLiteral("org.mozilla.firefox"), QStringLiteral("org.kde.kate"),
                                                              QStringLiteral("spotify")}},
                         {QStringLiteral("when"), qint64(saved.toSecsSinceEpoch())},
                         {QStringLiteral("clean"), true},
                         {QStringLiteral("windows"), 4}};
        kadunce.plan = {{QStringLiteral("accepted"), true},
                        {QStringLiteral("launch"), QStringList{QStringLiteral("org.mozilla.firefox"), QStringLiteral("org.kde.kate")}},
                        {QStringLiteral("alreadyOpen"), QStringList{QStringLiteral("spotify")}},
                        {QStringLiteral("count"), 2}};
        QVERIFY(standIn.registerObject(QStringLiteral("/Kadunce"), &kadunce, QDBusConnection::ExportAllSlots));
        const QString service = QStringLiteral("org.kde.KWin");
        QVERIFY(standIn.registerService(service));

        KadunceMemory memory(QDBusConnection::sessionBus(), service);
        std::optional<SavedSession> got;
        bool answered = false;
        memory.savedSession([&](const std::optional<SavedSession> &s) {
            got = s;
            answered = true;
        });
        QTRY_VERIFY(answered);
        QVERIFY(got);
        QVERIFY(got->available);
        QCOMPARE(got->count, 3);
        QCOMPARE(got->apps.last(), QStringLiteral("spotify"));
        QCOMPARE(got->when.toSecsSinceEpoch(), saved.toSecsSinceEpoch());
        QCOMPARE(got->windows, 4);

        // Bring back: Cards is armed first, then Core launches each app it
        // names, and the run ends when Cards says so.
        QStringList launched;
        std::optional<BringBackRun> run;
        memory.bringBack([&](const QString &id) { launched.append(id); }, [&](const BringBackRun &r) { run = r; });
        QTRY_COMPARE(launched.size(), 2);
        QVERIFY(memory.running());
        QCOMPARE(kadunce.calls.mid(kadunce.calls.indexOf(QStringLiteral("bringBack"))).first(), QStringLiteral("bringBack"));
        QVERIFY(!QUuid::fromString(kadunce.token).isNull());
        QVERIFY(!run);
        kadunce.finish(2, 0);
        QTRY_VERIFY(run.has_value());
        QVERIFY(run->accepted);
        QVERIFY(run->finished);
        QCOMPARE(run->launched, launched);
        QCOMPARE(run->alreadyOpen, QStringList{QStringLiteral("spotify")});
        QCOMPARE(run->placed, 2);
        QVERIFY(!memory.running());

        // Busy: nothing is launched.
        kadunce.plan = {{QStringLiteral("accepted"), false}, {QStringLiteral("reason"), QStringLiteral("busy")}};
        run.reset();
        launched.clear();
        memory.bringBack([&](const QString &id) { launched.append(id); }, [&](const BringBackRun &r) { run = r; });
        QTRY_VERIFY(run.has_value());
        QVERIFY(!run->accepted);
        QCOMPARE(run->reason, QStringLiteral("busy"));
        QVERIFY(launched.isEmpty());

        memory.dismissSavedSession();
        QTRY_VERIFY(kadunce.calls.contains(QStringLiteral("dismissSavedSession")));
        // The version is asked once.
        QCOMPARE(kadunce.calls.count(QStringLiteral("version")), 1);

        // A version Core doesn't know answers nothing.
        KadunceMemory newer(QDBusConnection::sessionBus(), service);
        kadunce.version = 2;
        answered = false;
        newer.savedSession([&](const std::optional<SavedSession> &s) {
            got = s;
            answered = true;
        });
        QTRY_VERIFY(answered);
        QVERIFY(!got);
        // And no Kadunce at all answers nothing either.
        KadunceMemory none(QDBusConnection::sessionBus(), QStringLiteral("org.example.NoKadunce"));
        answered = false;
        none.savedSession([&](const std::optional<SavedSession> &s) {
            got = s;
            answered = true;
        });
        QTRY_VERIFY(answered);
        QVERIFY(!got);
        standIn.unregisterService(service);
        QDBusConnection::disconnectFromBus(QStringLiteral("kadunce"));
    }

    void anAutomationIsOfferedOnceAfterThreeTimes()
    {
        Desk d;
        QVERIFY(d.core->watchesAutomations());
        QVERIFY(d.core->automations().isEmpty());
        // Twice is not a habit yet.
        d.core->seeOpenTogether({together(ThreeApps, 2, d.now)});
        QVERIFY(d.core->facts().isEmpty());
        // Three times: one offer, on the island, in the draft's words.
        d.core->seeOpenTogether({together(ThreeApps, 3, d.now),
                                 together({QStringLiteral("a"), QStringLiteral("b")}, 3, d.now.addDays(-2))});
        QVariantMap fact = d.fact(QStringLiteral("automations"));
        QCOMPARE(fact.value(QStringLiteral("line")).toString(), QStringLiteral("Open Mail, Notes and Firefox when you sign in?"));
        QCOMPARE(fact.value(QStringLiteral("detail")).toString(), QStringLiteral("You've started with these three times"));
        QCOMPARE(fact.value(QStringLiteral("actionText")).toString(), QStringLiteral("Yes, each time"));
        QCOMPARE(fact.value(QStringLiteral("dismissText")).toString(), QStringLiteral("Not now"));
        QCOMPARE(fact.value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(d.appsOpened.isEmpty());
        // One offer at a time: the other set waits.
        d.core->seeOpenTogether({together({QStringLiteral("a"), QStringLiteral("b")}, 5, d.now)});
        QCOMPARE(d.fact(QStringLiteral("automations")).value(QStringLiteral("line")).toString(),
                 QStringLiteral("Open Mail, Notes and Firefox when you sign in?"));

        // Yes: kept in the settings file with its own switch, nothing opened now.
        QVERIFY(d.core->act(QStringLiteral("automations"), QStringLiteral("open")));
        QVERIFY(d.fact(QStringLiteral("automations")).isEmpty());
        QVERIFY(d.appsOpened.isEmpty());
        const QList<Automation> kept = d.core->automations();
        QCOMPARE(kept.size(), 1);
        QCOMPARE(kept.first().group, QStringLiteral("Automation 1"));
        QCOMPARE(kept.first().apps, together(ThreeApps, 3, d.now).apps);
        QCOMPARE(kept.first().names.last(), QStringLiteral("Firefox"));
        QVERIFY(kept.first().enabled);
        QVERIFY(d.logbook().contains(QStringLiteral("Automations  You said yes: Mail, Notes and Firefox open when you sign in")));
        // Never offered again, even seen more.
        d.core->seeOpenTogether({together(ThreeApps, 9, d.now)});
        QVERIFY(d.fact(QStringLiteral("automations")).isEmpty());

        // The other set, the next day: Not now is for good too.
        d.now = d.now.addDays(1);
        d.core->seeOpenTogether({together(ThreeApps, 9, d.now), together({QStringLiteral("a"), QStringLiteral("b")}, 5, d.now)});
        fact = d.fact(QStringLiteral("automations"));
        QCOMPARE(fact.value(QStringLiteral("line")).toString(), QStringLiteral("Open A and B when you sign in?"));
        QCOMPARE(fact.value(QStringLiteral("detail")).toString(), QStringLiteral("You've started with these five times"));
        QVERIFY(d.core->act(QStringLiteral("automations"), QStringLiteral("not-now")));
        QCOMPARE(d.core->automations().size(), 1);
        d.now = d.now.addDays(1);
        d.core->seeOpenTogether({together({QStringLiteral("a"), QStringLiteral("b")}, 8, d.now)});
        QVERIFY(d.fact(QStringLiteral("automations")).isEmpty());
        QVERIFY(!d.core->act(QStringLiteral("automations"), QStringLiteral("open")));
    }

    void anOfferWaitsOutDoNotDisturbAndTodaysIsland()
    {
        Desk d;
        d.doNotDisturb = true;
        d.core->seeOpenTogether({together(ThreeApps, 3, d.now)});
        QVERIFY(d.core->facts().isEmpty());
        QVERIFY(d.ticker.isEmpty());
        d.doNotDisturb = false;
        d.core->seeOpenTogether({together(ThreeApps, 3, d.now)});
        QVERIFY(!d.fact(QStringLiteral("automations")).isEmpty());
        QVERIFY(d.core->act(QStringLiteral("automations"), QStringLiteral("dismiss")));
        // A second set the same day waits for tomorrow, never the ticker.
        d.core->seeOpenTogether({together({QStringLiteral("a"), QStringLiteral("b")}, 3, d.now)});
        QVERIFY(d.fact(QStringLiteral("automations")).isEmpty());
        QVERIFY(d.ticker.isEmpty());
        d.now = d.now.addDays(1);
        d.core->seeOpenTogether({together({QStringLiteral("a"), QStringLiteral("b")}, 3, d.now)});
        QVERIFY(!d.fact(QStringLiteral("automations")).isEmpty());
        // Survives a restart while it waits for an answer.
        d.make();
        QVERIFY(!d.fact(QStringLiteral("automations")).isEmpty());
        QVERIFY(d.core->act(QStringLiteral("automations"), QStringLiteral("open")));
        QCOMPARE(d.core->automations().first().apps, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    }

    void acceptedAutomationsOpenOnceASignInWithTheirSwitch()
    {
        Desk d;
        const QString rc = d.dir.filePath(QStringLiteral("corerc"));
        addAutomation(rc, together(ThreeApps, 3, d.now).apps, {QStringLiteral("Mail"), QStringLiteral("Notes"), QStringLiteral("Firefox")}, d.now);
        addAutomation(rc, {QStringLiteral("org.kde.kate"), QStringLiteral("org.mozilla.firefox")}, {}, d.now);
        QCOMPARE(readAutomations(rc).last().group, QStringLiteral("Automation 2"));
        // Firefox is coming back with Workspace Memory, so it isn't opened twice.
        QVERIFY(d.core->runAutomations({QStringLiteral("org.mozilla.firefox")}));
        QCOMPARE(d.appsOpened.size(), 1);
        QCOMPARE(d.appsOpened.first(), (QStringList{QStringLiteral("mail"), QStringLiteral("org.kde.notes"), QStringLiteral("org.kde.kate")}));
        QVERIFY(d.logbook().contains(QStringLiteral("Automations  Opened at sign-in, as you said yes to: Mail, Notes and Firefox; Kate and Firefox.")));
        // Once a sign-in, however often Core starts in it.
        d.make();
        QVERIFY(!d.core->runAutomations());
        QCOMPARE(d.appsOpened.size(), 1);
        // Its switch in Settings: off, it opens nothing.
        {
            QSettings settings(rc, QSettings::IniFormat);
            settings.setValue(QStringLiteral("Automation 1/Enabled"), false);
            settings.setValue(QStringLiteral("Automation 2/Enabled"), false);
        }
        d.signIn = QStringLiteral("2");
        QVERIFY(!d.core->runAutomations());
        QCOMPARE(d.appsOpened.size(), 1);
        // The topic switched off: nothing is offered or opened.
        {
            QSettings settings(rc, QSettings::IniFormat);
            settings.setValue(QStringLiteral("Automation 1/Enabled"), true);
            settings.setValue(QStringLiteral("Automations/Enabled"), false);
        }
        d.signIn = QStringLiteral("3");
        QVERIFY(!d.core->watchesAutomations());
        QVERIFY(!d.core->runAutomations());
        d.core->seeOpenTogether({together({QStringLiteral("x"), QStringLiteral("y")}, 4, d.now)});
        QVERIFY(d.core->facts().isEmpty());
        QCOMPARE(d.appsOpened.size(), 1);
    }

    void kadunceOpenTogetherComesOffTheBus()
    {
        QDBusConnection standIn = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("kadunce"));
        StandInKadunce kadunce;
        const qint64 seen = QDateTime::currentSecsSinceEpoch();
        kadunce.together = {QVariantMap{{QStringLiteral("apps"), QStringList{QStringLiteral("org.mozilla.firefox"), QStringLiteral("mail")}},
                                        {QStringLiteral("count"), 3},
                                        {QStringLiteral("lastSeen"), seen}},
                            QVariantMap{{QStringLiteral("apps"), QStringList{QStringLiteral("solo")}},
                                        {QStringLiteral("count"), 7},
                                        {QStringLiteral("lastSeen"), seen}}};
        QVERIFY(standIn.registerObject(QStringLiteral("/Kadunce"), &kadunce, QDBusConnection::ExportAllSlots));
        QVERIFY(standIn.registerService(QStringLiteral("org.kde.KWin")));
        KadunceMemory memory(QDBusConnection::sessionBus());
        std::optional<QList<OpenSet>> sets;
        bool answered = false;
        memory.openTogether([&](const std::optional<QList<OpenSet>> &s) {
            sets = s;
            answered = true;
        });
        QTRY_VERIFY(answered);
        QVERIFY(sets);
        // A set of one app is no set.
        QCOMPARE(sets->size(), 1);
        QCOMPARE(sets->first().apps, (QStringList{QStringLiteral("mail"), QStringLiteral("org.mozilla.firefox")}));
        QCOMPARE(sets->first().count, 3);
        QCOMPARE(sets->first().lastSeen.toSecsSinceEpoch(), seen);
        // Through Core, to the offer.
        Desk d;
        d.core->seeOpenTogether(*sets);
        QCOMPARE(d.fact(QStringLiteral("automations")).value(QStringLiteral("line")).toString(),
                 QStringLiteral("Open Mail and Firefox when you sign in?"));
        standIn.unregisterService(QStringLiteral("org.kde.KWin"));
        QDBusConnection::disconnectFromBus(QStringLiteral("kadunce"));
    }

    void layoutsAreKeptInTheSettingsFile()
    {
        Desk d;
        const QString rc = d.dir.filePath(QStringLiteral("corerc"));
        d.setNotice(QStringLiteral("60"));
        QSignalSpy changed(d.core.get(), &Core::layoutsChanged);
        QVERIFY(d.core->layouts().isEmpty());
        // An empty name, or no groups, saves nothing.
        QCOMPARE(d.core->saveLayout(QStringLiteral("  "), QStringLiteral(R"([{"apps":["a"],"stack":false}])"), false), QString());
        QCOMPARE(d.core->saveLayout(QStringLiteral("Coding"), QStringLiteral("[]"), false), QString());
        QCOMPARE(d.core->saveLayout(QStringLiteral("Coding"), QStringLiteral("not json"), false), QString());
        QCOMPARE(changed.size(), 0);

        // An id with .desktop loses it, an app is kept once, in its first
        // group, and a group of one is no Stack.
        const QString coding = d.core->saveLayout(
            QStringLiteral("Coding"),
            QStringLiteral(R"([{"apps":["com.mitchellh.ghostty.desktop","com.anthropic.claude"],"stack":true,"names":["x"]},)"
                           R"({"apps":["org.mozilla.firefox","com.anthropic.claude"],"stack":true},{"apps":[],"stack":false}])"),
            false);
        QCOMPARE(coding, QStringLiteral("1"));
        QCOMPARE(changed.size(), 1);
        const QString reading = d.core->saveLayout(QStringLiteral("Reading"),
                                                   QStringLiteral(R"([{"apps":["org.kde.okular"],"stack":false}])"), true);
        QCOMPARE(reading, QStringLiteral("2"));
        // Qt writes the group's space as %20, as it does for Automations.
        QVERIFY(read(rc).contains(QStringLiteral("[Layout%201]")));
        QVERIFY(read(rc).contains(QStringLiteral("Groups=")));

        const QList<QVariantMap> layouts = d.core->layoutsForBus();
        QCOMPARE(layouts.size(), 2);
        QCOMPARE(layouts.first().value(QStringLiteral("id")).toString(), QStringLiteral("1"));
        QCOMPARE(layouts.first().value(QStringLiteral("name")).toString(), QStringLiteral("Coding"));
        QVERIFY(!layouts.first().value(QStringLiteral("atSignIn")).toBool());
        const QList<QVariantMap> groups = layouts.first().value(QStringLiteral("groups")).value<QList<QVariantMap>>();
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.at(0).value(QStringLiteral("apps")).toStringList(),
                 (QStringList{QStringLiteral("com.mitchellh.ghostty"), QStringLiteral("com.anthropic.claude")}));
        QCOMPARE(groups.at(0).value(QStringLiteral("names")).toStringList(),
                 (QStringList{QStringLiteral("Ghostty"), QStringLiteral("Claude")}));
        QVERIFY(groups.at(0).value(QStringLiteral("stack")).toBool());
        QCOMPARE(groups.at(1).value(QStringLiteral("names")).toStringList(), QStringList{QStringLiteral("Firefox")});
        QVERIFY(!groups.at(1).value(QStringLiteral("stack")).toBool());
        QVERIFY(layouts.last().value(QStringLiteral("atSignIn")).toBool());

        // The same name, whatever its case, replaces that layout and keeps
        // its id.
        QCOMPARE(d.core->saveLayout(QStringLiteral("coding"), QStringLiteral(R"([{"apps":["org.kde.kate"],"stack":false}])"), true),
                 QStringLiteral("1"));
        QCOMPARE(d.core->layouts().size(), 2);
        QCOMPARE(d.core->layouts().first().name, QStringLiteral("coding"));
        QCOMPARE(d.core->layouts().first().groups, (QList<LayoutGroup>{{{QStringLiteral("org.kde.kate")}, false}}));
        QVERIFY(d.core->layouts().first().atSignIn);
        QCOMPARE(d.core->layouts().first().added, d.now);

        // Its switch, and forgetting it.
        QVERIFY(d.core->setLayoutAtSignIn(QStringLiteral("2"), false));
        QVERIFY(!d.core->layouts().last().atSignIn);
        QVERIFY(!d.core->setLayoutAtSignIn(QStringLiteral("9"), true));
        QVERIFY(d.core->removeLayout(QStringLiteral("1")));
        QVERIFY(!d.core->removeLayout(QStringLiteral("1")));
        QVERIFY(!d.core->removeLayout(QStringLiteral("General")));
        QCOMPARE(d.core->layouts().size(), 1);
        QCOMPARE(d.core->layouts().first().id, QStringLiteral("2"));
        QCOMPARE(changed.size(), 5);
        // The settings Core reads at the top of the file are untouched.
        QCOMPARE(d.core->notice(), 60);
        QVERIFY(d.logbook().contains(QStringLiteral("Layouts  Saved Coding: 3 apps in 2 groups.")));
        QVERIFY(d.logbook().contains(QStringLiteral("Layouts  Saved Reading: 1 app in 1 group, opening when you sign in.")));
        QVERIFY(d.logbook().contains(QStringLiteral("Layouts  Removed coding.")));
    }

    void theCurrentDeskIsCardsWithTheAppsNames()
    {
        Desk d;
        QVariantMap desk;
        d.core->currentLayout([&desk](const QVariantMap &answer) { desk = answer; });
        QVERIFY(!desk.value(QStringLiteral("available")).toBool());
        QVERIFY(desk.value(QStringLiteral("groups")).value<QList<QVariantMap>>().isEmpty());
        d.cards = CurrentDesk{QStringLiteral("desktop-1"),
                              {{{QStringLiteral("com.mitchellh.ghostty"), QStringLiteral("com.anthropic.claude")}, true},
                               {{QStringLiteral("org.mozilla.firefox")}, false}}};
        d.core->currentLayout([&desk](const QVariantMap &answer) { desk = answer; });
        QVERIFY(desk.value(QStringLiteral("available")).toBool());
        QCOMPARE(desk.value(QStringLiteral("workspace")).toString(), QStringLiteral("desktop-1"));
        const QList<QVariantMap> groups = desk.value(QStringLiteral("groups")).value<QList<QVariantMap>>();
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.first().value(QStringLiteral("names")).toStringList(),
                 (QStringList{QStringLiteral("Ghostty"), QStringLiteral("Claude")}));
        QCOMPARE(groups.last().value(QStringLiteral("names")).toStringList(), QStringList{QStringLiteral("Firefox")});
    }

    void openingALayoutLaunchesWhatCardsNames()
    {
        Desk d;
        const QString id = d.core->saveLayout(
            QStringLiteral("Coding"),
            QStringLiteral(R"([{"apps":["com.mitchellh.ghostty","com.anthropic.claude"],"stack":true},{"apps":["mail"],"stack":false}])"),
            false);
        QSignalSpy opened(d.core.get(), &Core::layoutOpened);
        std::optional<bool> accepted;
        QVERIFY(!d.core->openLayout(QStringLiteral("7"), [&accepted](bool a) { accepted = a; }));
        QVERIFY(d.arranged.isEmpty());
        QVERIFY(!accepted);

        // Cards is asked to arrange it; Core launches what Cards names.
        d.plan.launch = {QStringLiteral("com.anthropic.claude"), QStringLiteral("mail")};
        d.plan.alreadyOpen = {QStringLiteral("com.mitchellh.ghostty")};
        QVERIFY(d.core->openLayout(id, [&accepted](bool a) { accepted = a; }));
        QCOMPARE(accepted, std::optional<bool>(true));
        QCOMPARE(d.arranged.size(), 1);
        QCOMPARE(d.arranged.first().first().apps.size(), 2);
        QVERIFY(d.arranged.first().first().stack);
        QCOMPARE(d.appsOpened, (QList<QStringList>{{QStringLiteral("com.anthropic.claude"), QStringLiteral("mail")}}));
        // One run at a time.
        QVERIFY(!d.core->openLayout(id));
        QCOMPARE(d.arranged.size(), 1);

        // The run ends with Mail missing: the island is told its name.
        d.endRun(2, {QStringLiteral("mail")});
        QCOMPARE(opened.size(), 1);
        QCOMPARE(opened.first().at(0).toString(), id);
        QCOMPARE(opened.first().at(1).toInt(), 2);
        QCOMPARE(opened.first().at(2).toInt(), 1);
        QCOMPARE(opened.first().at(3).toStringList(), QStringList{QStringLiteral("Mail")});
        QVERIFY(d.logbook().contains(QStringLiteral("Layouts  Opened Coding: launched 2 apps, with 1 already open; 2 in place, 1 missed (Mail).")));

        // Cards busy: refused, nothing launched, nothing opened.
        d.plan = {false, {}, {}, QStringLiteral("busy")};
        accepted.reset();
        QVERIFY(d.core->openLayout(id, [&accepted](bool a) { accepted = a; }));
        QCOMPARE(accepted, std::optional<bool>(false));
        QCOMPARE(d.appsOpened.size(), 1);
        QCOMPARE(opened.size(), 1);
        QVERIFY(d.logbook().contains(QStringLiteral("Layouts  Cards didn't open Coding: it was arranging the desk already.")));

        // During a bring-back, Core refuses at once.
        d.plan = {true, {}, {}, {}};
        d.core->seeSavedSession(session(d.now, 14, FourApps));
        QVERIFY(d.core->act(QStringLiteral("memory"), QStringLiteral("open")));
        QVERIFY(!d.core->openLayout(id));
        BringBackRun run;
        run.accepted = true;
        run.finished = true;
        d.core->broughtBack(run);
        QVERIFY(d.core->openLayout(id));
    }

    void layoutsOpenAtSignInOnceTheMemoryQuestionIsSettled()
    {
        Desk d;
        const QString rc = d.dir.filePath(QStringLiteral("corerc"));
        d.core->saveLayout(QStringLiteral("Coding"), QStringLiteral(R"([{"apps":["org.kde.kate"],"stack":false}])"), true);
        d.core->saveLayout(QStringLiteral("Music"), QStringLiteral(R"([{"apps":["spotify"],"stack":false}])"), false);
        d.core->saveLayout(QStringLiteral("Mail"), QStringLiteral(R"([{"apps":["mail","org.mozilla.firefox"],"stack":true}])"), true);
        // An automation opened Firefox this sign-in already.
        addAutomation(rc, {QStringLiteral("org.mozilla.firefox"), QStringLiteral("org.kde.notes")}, {}, d.now);

        // The question stands on the island: the layouts wait for the answer.
        d.core->seeSavedSession(session(d.now, 14, FourApps));
        QVERIFY(!d.core->memorySettled());
        QVERIFY(d.core->runAutomations());
        QVERIFY(d.core->openLayoutsAtSignIn());
        QVERIFY(d.arranged.isEmpty());
        // Bring back: still waiting, until Cards has brought them back.
        QVERIFY(d.core->act(QStringLiteral("memory"), QStringLiteral("open")));
        QVERIFY(d.arranged.isEmpty());
        BringBackRun run;
        run.accepted = true;
        run.finished = true;
        d.core->broughtBack(run);
        QVERIFY(d.core->memorySettled());
        // Then each switched on, one after another.
        QCOMPARE(d.arranged.size(), 1);
        QCOMPARE(d.arranged.first().first().apps, QStringList{QStringLiteral("org.kde.kate")});
        d.endRun(1);
        QCOMPARE(d.arranged.size(), 2);
        QCOMPARE(d.arranged.last().first().apps, (QStringList{QStringLiteral("mail"), QStringLiteral("org.mozilla.firefox")}));
        d.endRun(2);
        QCOMPARE(d.arranged.size(), 2);

        // Once a sign-in, however often Core starts in it.
        d.make();
        QVERIFY(!d.core->openLayoutsAtSignIn());
        QCOMPARE(d.arranged.size(), 2);

        // Not now settles it too; and Firefox, which an automation is
        // opening, isn't launched a second time.
        d.signIn = QStringLiteral("2");
        d.make();
        d.now = d.now.addDays(1);
        d.core->seeSavedSession(session(d.now, 12, FourApps));
        QVERIFY(d.core->runAutomations());
        d.plan.launch = {QStringLiteral("org.kde.kate")};
        QVERIFY(d.core->openLayoutsAtSignIn());
        QCOMPARE(d.arranged.size(), 2);
        QVERIFY(d.core->act(QStringLiteral("memory"), QStringLiteral("not-now")));
        QCOMPARE(d.arranged.size(), 3);
        QCOMPARE(d.appsOpened.last(), QStringList{QStringLiteral("org.kde.kate")});
        d.plan.launch = {QStringLiteral("mail"), QStringLiteral("org.mozilla.firefox")};
        d.endRun(1);
        QCOMPARE(d.arranged.size(), 4);
        QCOMPARE(d.appsOpened.last(), QStringList{QStringLiteral("mail")});
        d.endRun(2);

        // With nothing offered, they open straight away.
        d.signIn = QStringLiteral("3");
        d.make();
        d.core->seeSavedSession(SavedSession());
        QVERIFY(d.core->openLayoutsAtSignIn());
        QCOMPARE(d.arranged.size(), 5);
    }

    void layoutsGoThroughCardsAndTheBus()
    {
        qDBusRegisterMetaType<QList<QVariantMap>>();
        QDBusConnection standIn = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("kadunce"));
        QVERIFY(standIn.isConnected());
        StandInKadunce kadunce;
        kadunce.desk = {{QStringLiteral("workspace"), QStringLiteral("desktop-2")},
                        {QStringLiteral("groups"),
                         QVariant::fromValue(QList<QVariantMap>{
                             {{QStringLiteral("apps"), QStringList{QStringLiteral("com.mitchellh.ghostty"), QStringLiteral("com.anthropic.claude")}},
                              {QStringLiteral("stack"), true}},
                             {{QStringLiteral("apps"), QStringList{QStringLiteral("mail")}}, {QStringLiteral("stack"), false}}})}};
        kadunce.layoutPlan = {{QStringLiteral("accepted"), true},
                              {QStringLiteral("launch"), QStringList{QStringLiteral("com.anthropic.claude"), QStringLiteral("mail")}},
                              {QStringLiteral("alreadyOpen"), QStringList{QStringLiteral("com.mitchellh.ghostty")}},
                              {QStringLiteral("reason"), QString()}};
        QVERIFY(standIn.registerObject(QStringLiteral("/Kadunce"), &kadunce, QDBusConnection::ExportAllSlots));
        const QString kwin = QStringLiteral("org.kde.KWin");
        QVERIFY(standIn.registerService(kwin));

        // Core, with Cards on the bus, offered on the bus.
        KadunceLayouts cards(QDBusConnection::sessionBus(), kwin);
        Desk d;
        d.wire = [&cards](Core::Hooks &hooks) {
            hooks.currentLayout = [&cards](std::function<void(std::optional<CurrentDesk>)> answered) {
                cards.currentLayout(std::move(answered));
            };
            hooks.arrangeLayout = [&cards](const QList<LayoutGroup> &groups, std::function<void(const LayoutPlan &)> armed,
                                           std::function<void(const LayoutRun &)> done) {
                cards.arrange(groups, std::move(armed), std::move(done));
            };
        };
        d.make();
        QDBusConnection bus = QDBusConnection::sessionBus();
        CoreService service(d.core.get());
        QVERIFY(service.publish(bus));
        QVERIFY(bus.registerService(QLatin1String(CoreService::Service)));
        QSignalSpy opened(&service, &CoreService::LayoutOpened);
        QSignalSpy changed(&service, &CoreService::LayoutsChanged);

        // A caller of its own, as Search is.
        QDBusConnection search = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("search"));
        const auto ask = [&search](const QString &method, const QVariantList &arguments = {}) {
            QDBusMessage call = QDBusMessage::createMethodCall(QLatin1String(CoreService::Service), QLatin1String(CoreService::Path),
                                                               QStringLiteral("io.github.carlsonjm.SplitRock.Core"), method);
            call.setArguments(arguments);
            return search.asyncCall(call, 5000);
        };

        // The desk now, from Cards, with the apps' names.
        QDBusPendingReply<QVariantMap> current = ask(QStringLiteral("CurrentLayout"));
        QTRY_VERIFY(current.isFinished());
        QVERIFY2(!current.isError(), qPrintable(current.error().message()));
        QVERIFY(current.value().value(QStringLiteral("available")).toBool());
        QCOMPARE(current.value().value(QStringLiteral("workspace")).toString(), QStringLiteral("desktop-2"));
        QList<QVariantMap> groups;
        current.value().value(QStringLiteral("groups")).value<QDBusArgument>() >> groups;
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.first().value(QStringLiteral("names")).toStringList(),
                 (QStringList{QStringLiteral("Ghostty"), QStringLiteral("Claude")}));
        QVERIFY(groups.first().value(QStringLiteral("stack")).toBool());

        // Saved as Search sends it, and listed.
        QDBusPendingReply<QString> saved = ask(
            QStringLiteral("SaveLayout"),
            {QStringLiteral("Coding"),
             QStringLiteral(R"([{"apps":["com.mitchellh.ghostty","com.anthropic.claude"],"stack":true},{"apps":["mail"],"stack":false}])"),
             false});
        QTRY_VERIFY(saved.isFinished());
        QCOMPARE(saved.value(), QStringLiteral("1"));
        QTRY_COMPARE(changed.size(), 1);
        QDBusPendingReply<QList<QVariantMap>> listed = ask(QStringLiteral("Layouts"));
        QTRY_VERIFY(listed.isFinished());
        QVERIFY2(!listed.isError(), qPrintable(listed.error().message()));
        QCOMPARE(listed.value().size(), 1);
        QCOMPARE(listed.value().first().value(QStringLiteral("name")).toString(), QStringLiteral("Coding"));
        QDBusPendingReply<bool> switched = ask(QStringLiteral("SetLayoutAtSignIn"), {QStringLiteral("1"), true});
        QTRY_VERIFY(switched.isFinished());
        QVERIFY(switched.value());

        // Opened: Cards gets the groups, Core launches what it names, and
        // the run's end comes back as LayoutOpened with what was missed.
        QDBusPendingReply<bool> unknown = ask(QStringLiteral("OpenLayout"), {QStringLiteral("5")});
        QTRY_VERIFY(unknown.isFinished());
        QVERIFY(!unknown.value());
        QDBusPendingReply<bool> open = ask(QStringLiteral("OpenLayout"), {QStringLiteral("1")});
        QTRY_VERIFY(open.isFinished());
        QVERIFY2(!open.isError(), qPrintable(open.error().message()));
        QVERIFY(open.value());
        QCOMPARE(kadunce.arrangedGroups.size(), 2);
        QCOMPARE(kadunce.arrangedGroups.first().value(QStringLiteral("apps")).toStringList().size(), 2);
        QVERIFY(kadunce.arrangedGroups.first().value(QStringLiteral("stack")).toBool());
        QVERIFY(!QUuid::fromString(kadunce.layoutToken).isNull());
        QCOMPARE(d.appsOpened, (QList<QStringList>{{QStringLiteral("com.anthropic.claude"), QStringLiteral("mail")}}));
        kadunce.placed(QStringLiteral("com.mitchellh.ghostty"), true);
        kadunce.placed(QStringLiteral("com.anthropic.claude"), true);
        kadunce.placed(QStringLiteral("mail"), false);
        kadunce.layoutDone(2, 1);
        QTRY_COMPARE(opened.size(), 1);
        QCOMPARE(opened.first().at(0).toString(), QStringLiteral("1"));
        QCOMPARE(opened.first().at(1).toInt(), 2);
        QCOMPARE(opened.first().at(2).toInt(), 1);
        QCOMPARE(opened.first().at(3).toStringList(), QStringList{QStringLiteral("Mail")});
        QVERIFY(!cards.running());

        // Cards busy: OpenLayout answers false.
        kadunce.layoutPlan = {{QStringLiteral("accepted"), false}, {QStringLiteral("reason"), QStringLiteral("busy")}};
        QDBusPendingReply<bool> busy = ask(QStringLiteral("OpenLayout"), {QStringLiteral("1")});
        QTRY_VERIFY(busy.isFinished());
        QVERIFY(!busy.value());
        QCOMPARE(opened.size(), 1);

        QDBusPendingReply<bool> removed = ask(QStringLiteral("RemoveLayout"), {QStringLiteral("1")});
        QTRY_VERIFY(removed.isFinished());
        QVERIFY(removed.value());
        QTRY_COMPARE(changed.size(), 3);
        // The layout version is asked once.
        QCOMPARE(kadunce.calls.count(QStringLiteral("layoutVersion")), 1);

        // Without Cards, CurrentLayout says it isn't available.
        standIn.unregisterService(kwin);
        KadunceLayouts none(QDBusConnection::sessionBus(), QStringLiteral("org.example.NoKadunce"));
        std::optional<CurrentDesk> nothing = CurrentDesk();
        bool answered = false;
        none.currentLayout([&](const std::optional<CurrentDesk> &desk) {
            nothing = desk;
            answered = true;
        });
        QTRY_VERIFY(answered);
        QVERIFY(!nothing);

        QDBusConnection::disconnectFromBus(QStringLiteral("search"));
        bus.unregisterService(QLatin1String(CoreService::Service));
        bus.unregisterObject(QLatin1String(CoreService::Path));
        QDBusConnection::disconnectFromBus(QStringLiteral("kadunce"));
    }

    void cardsArrangesOneDeskAtATimeAndNamesWhatMissed()
    {
        QDBusConnection standIn = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("kadunce"));
        StandInKadunce kadunce;
        kadunce.layoutPlan = {{QStringLiteral("accepted"), true},
                              {QStringLiteral("launch"), QStringList{QStringLiteral("a"), QStringLiteral("b")}}};
        QVERIFY(standIn.registerObject(QStringLiteral("/Kadunce"), &kadunce, QDBusConnection::ExportAllSlots));
        QVERIFY(standIn.registerService(QStringLiteral("org.kde.KWin")));
        KadunceLayouts cards(QDBusConnection::sessionBus());
        std::optional<LayoutPlan> armed;
        std::optional<LayoutRun> run;
        cards.arrange({{{QStringLiteral("a"), QStringLiteral("b")}, true}}, [&](const LayoutPlan &p) { armed = p; },
                      [&](const LayoutRun &r) { run = r; });
        QTRY_VERIFY(armed.has_value());
        QCOMPARE(armed->launch, (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QVERIFY(cards.running());
        // A second arrangement while one runs is turned down at once.
        std::optional<LayoutRun> second;
        cards.arrange({{{QStringLiteral("c")}, false}}, {}, [&](const LayoutRun &r) { second = r; });
        QVERIFY(second.has_value());
        QVERIFY(!second->accepted);
        QCOMPARE(second->reason, QStringLiteral("busy"));
        // A finish for another run is not this one's.
        const QString token = kadunce.layoutToken;
        kadunce.layoutToken = QStringLiteral("someone-else");
        kadunce.layoutDone(2, 0);
        kadunce.layoutToken = token;
        kadunce.placed(QStringLiteral("a"), true);
        QTest::qWait(200);
        QVERIFY(!run);
        // b never said it arrived, so it is the one missed.
        kadunce.layoutDone(1, 1);
        QTRY_VERIFY(run.has_value());
        QVERIFY(run->accepted);
        QVERIFY(run->finished);
        QCOMPARE(run->placed, 1);
        QCOMPARE(run->missedApps, QStringList{QStringLiteral("b")});
        QVERIFY(!cards.running());

        // A version Core doesn't know arranges nothing.
        kadunce.version = 2;
        KadunceLayouts newer(QDBusConnection::sessionBus());
        run.reset();
        armed.reset();
        newer.arrange({{{QStringLiteral("a")}, false}}, [&](const LayoutPlan &p) { armed = p; },
                      [&](const LayoutRun &r) { run = r; });
        QTRY_VERIFY(run.has_value());
        QVERIFY(!run->accepted);
        QVERIFY(!armed);
        QVERIFY(!kadunce.calls.contains(QStringLiteral("cancelLayout")));
        standIn.unregisterService(QStringLiteral("org.kde.KWin"));
        QDBusConnection::disconnectFromBus(QStringLiteral("kadunce"));
    }

    void focusSaysOnceWhatCameInDuringDoNotDisturb()
    {
        Desk d;
        QVERIFY(d.core->watchesFocus());
        d.core->focusEnded(7);
        const QVariantMap focus = d.fact(QStringLiteral("focus"));
        QCOMPARE(focus.value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        QCOMPARE(focus.value(QStringLiteral("line")).toString(), QStringLiteral("While you were focused"));
        QCOMPARE(focus.value(QStringLiteral("detail")).toString(), QStringLiteral("7 notifications"));
        QCOMPARE(focus.value(QStringLiteral("actionText")).toString(), QStringLiteral("See them"));
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Focus  Showed the island: While you were focused, 7 notifications.")));
        // See them opens the notifications, and it is no longer news.
        QVERIFY(d.core->act(QStringLiteral("focus"), QStringLiteral("open")));
        QCOMPARE(d.notificationsOpened, 1);
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Focus  You opened the notifications.")));
        QVERIFY(!d.core->act(QStringLiteral("focus"), QStringLiteral("open")));
        // A second time the same day goes to the ticker.
        d.now = d.now.addSecs(3600);
        d.core->focusEnded(1);
        QCOMPARE(d.ticker, QStringList{QStringLiteral("Focus · While you were focused | 1 notification")});
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());
        // The next day, the island again; it survives a restart.
        d.now = d.now.addDays(1);
        d.core->focusEnded(2);
        d.make();
        QCOMPARE(d.fact(QStringLiteral("focus")).value(QStringLiteral("detail")).toString(), QStringLiteral("2 notifications"));
        QVERIFY(d.core->act(QStringLiteral("focus"), QStringLiteral("dismiss")));
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());
    }

    void focusNamesWhatCoreHeldBack()
    {
        Desk d;
        // The drive passes the island's mark during Do Not Disturb: the ticker only.
        d.doNotDisturb = true;
        d.look(92);
        QCOMPARE(d.ticker.size(), 1);
        d.doNotDisturb = false;
        d.core->focusEnded(3);
        QCOMPARE(d.fact(QStringLiteral("focus")).value(QStringLiteral("detail")).toString(),
                 QStringLiteral("3 notifications · Storage waiting"));
        QVERIFY(d.core->act(QStringLiteral("focus"), QStringLiteral("dismiss")));
        // Held once is said once.
        d.now = d.now.addDays(1);
        d.core->focusEnded(0);
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Focus  Do Not Disturb ended; nothing came in.")));
        // With nothing counting notifications, what was held opens its own card.
        d.doNotDisturb = true;
        d.updates(12, 30);
        d.doNotDisturb = false;
        d.core->focusEnded(-1);
        const QVariantMap focus = d.fact(QStringLiteral("focus"));
        QCOMPARE(focus.value(QStringLiteral("detail")).toString(), QStringLiteral("Updates waiting"));
        QCOMPARE(focus.value(QStringLiteral("actionText")).toString(), QStringLiteral("Open"));
        QVERIFY(d.core->act(QStringLiteral("focus"), QStringLiteral("open")));
        QCOMPARE(d.opened, QStringList{QStringLiteral("update")});
        QCOMPARE(d.notificationsOpened, 0);
    }

    // Ended inside the notifications, where what came in is in front of the
    // person: no island. Opening the notifications any other way takes the
    // island down.
    void focusSeenInTheNotificationsNeedsNoIsland()
    {
        Desk d;
        d.doNotDisturb = true;
        d.look(92);
        d.doNotDisturb = false;
        d.core->focusEnded(5, true);
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());
        QCOMPARE(d.ticker.size(), 1);
        QVERIFY(d.logbook().contains(QStringLiteral("Focus  Do Not Disturb ended in the notifications; 5 came in, shown there.")));
        // What was held is not said again later.
        d.now = d.now.addSecs(600);
        d.core->focusEnded(0);
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());

        d.now = d.now.addSecs(600);
        d.core->focusEnded(3);
        QCOMPARE(d.fact(QStringLiteral("focus")).value(QStringLiteral("attention")).toString(), QStringLiteral("island"));
        d.core->notificationsSeen();
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());
        QVERIFY(d.logbook().contains(QStringLiteral("Focus  You saw them in the notifications.")));
        QCOMPARE(d.notificationsOpened, 0);
        // Seen again: nothing more to do.
        d.core->notificationsSeen();
        QVERIFY(!d.core->act(QStringLiteral("focus"), QStringLiteral("open")));
    }

    void focusSwitchedOffIsSilent()
    {
        Desk d;
        {
            QSettings settings(d.dir.filePath(QStringLiteral("corerc")), QSettings::IniFormat);
            settings.setValue(QStringLiteral("Focus/Enabled"), false);
        }
        QVERIFY(!d.core->watchesFocus());
        d.core->focusEnded(5);
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());
        QVERIFY(d.ticker.isEmpty());
        QVERIFY(!d.core->act(QStringLiteral("focus"), QStringLiteral("open")));
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
        // Do Not Disturb ended, as the notifications tell it.
        QDBusMessage ended = QDBusMessage::createMethodCall(QLatin1String(CoreService::Service), QLatin1String(CoreService::Path),
                                                            QStringLiteral("io.github.carlsonjm.SplitRock.Core"),
                                                            QStringLiteral("FocusEnded"));
        ended.setArguments({QVariant::fromValue(4u)});
        const QDBusMessage reply = bus.call(ended);
        QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
        QCOMPARE(d.fact(QStringLiteral("focus")).value(QStringLiteral("detail")).toString(), QStringLiteral("4 notifications"));
        // Opening the notifications takes it down.
        const QDBusMessage seen = bus.call(QDBusMessage::createMethodCall(
            QLatin1String(CoreService::Service), QLatin1String(CoreService::Path), QStringLiteral("io.github.carlsonjm.SplitRock.Core"),
            QStringLiteral("NotificationsSeen")));
        QVERIFY2(seen.type() == QDBusMessage::ReplyMessage, qPrintable(seen.errorMessage()));
        QVERIFY(d.fact(QStringLiteral("focus")).isEmpty());
        bus.unregisterService(QLatin1String(CoreService::Service));
        bus.unregisterObject(QLatin1String(CoreService::Path));
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
