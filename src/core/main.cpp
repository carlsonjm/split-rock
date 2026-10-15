// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core, Split Rock's front desk: a background program with no window, one for
// each person, that looks at the computer on a schedule and tells the
// desktop's surfaces when a fact deserves attention (docs/CORE.md). It runs as
// the person, never as the administrator, and never calls a language model.
//
//   split-rock-core [--folder DIR] [--settings FILE] [--drive DIR] [--every MINUTES]
//                   [--updates-every HOURS]
//
// --folder    where its logbook and looks are kept
//             (default: split-rock/core in the person's data folder)
// --settings  the settings file where a topic is switched off
//             (default: split-rock-corerc in the person's settings folder)
// --drive     a folder on the drive to watch (default: the home folder)
// --every     minutes between looks at the drive (default: 30)
// --updates-every
//             hours between looks for updates (default: 6); it also looks a
//             little after pacman's log stops changing, so a finished update
//             is seen at once
//
// Core runs once: a second start ends at once.

#include "core/Core.h"
#include "core/CoreService.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <chrono>

using namespace splitrock::core;

namespace {

const QString Notifications = QStringLiteral("org.freedesktop.Notifications");
const QString NotificationsPath = QStringLiteral("/org/freedesktop/Notifications");

// Split Rock's own program, which opens a card on its own: beside Core in a
// build, where it was installed otherwise.
QString splitRockProgram()
{
    const QDir here(QCoreApplication::applicationDirPath());
    if (QFileInfo(here.filePath(QStringLiteral("split-rock"))).isExecutable())
        return here.filePath(QStringLiteral("split-rock"));
    return QStringLiteral(SPLIT_ROCK_INSTALLED_PROGRAM);
}

// Notifications Core sent, by id, so a tap on one opens its topic's card.
class Ticker : public QObject
{
    Q_OBJECT

public:
    Ticker(QDBusConnection bus, std::function<void(const QString &topic)> opened)
        : m_bus(bus)
        , m_opened(std::move(opened))
    {
        m_bus.connect(Notifications, NotificationsPath, Notifications, QStringLiteral("ActionInvoked"), this,
                      SLOT(invoked(uint, QString)));
    }

    void send(const QString &topic, const QString &summary, const QString &body)
    {
        const bool updates = topic == QLatin1String("updates");
        QDBusMessage call = QDBusMessage::createMethodCall(Notifications, NotificationsPath, Notifications,
                                                           QStringLiteral("Notify"));
        const QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("io.github.carlsonjm.SplitRock")},
                                {QStringLiteral("urgency"), QVariant::fromValue<uchar>(1)}};
        // Each topic replaces only its own last notification.
        const uint last = m_last.value(topic);
        call.setArguments({updates ? QStringLiteral("Updates") : QStringLiteral("Storage"), last,
                           updates ? QStringLiteral("system-software-update") : QStringLiteral("drive-harddisk"),
                           summary, body,
                           QStringList{QStringLiteral("default"),
                                       updates ? QStringLiteral("Check for updates") : QStringLiteral("Free up space")},
                           hints, -1});
        const QDBusReply<uint> reply = m_bus.call(call, QDBus::Block, 2000);
        if (reply.isValid())
            m_last.insert(topic, reply.value());
    }

    bool doNotDisturb()
    {
        QDBusMessage call = QDBusMessage::createMethodCall(Notifications, NotificationsPath,
                                                           QStringLiteral("org.freedesktop.DBus.Properties"),
                                                           QStringLiteral("Get"));
        call.setArguments({Notifications, QStringLiteral("Inhibited")});
        const QDBusReply<QDBusVariant> reply = m_bus.call(call, QDBus::Block, 1000);
        return reply.isValid() && reply.value().variant().toBool();
    }

private Q_SLOTS:
    void invoked(uint id, const QString &action)
    {
        if (id == 0 || action != QLatin1String("default"))
            return;
        for (auto it = m_last.cbegin(); it != m_last.cend(); ++it) {
            if (it.value() == id)
                m_opened(it.key());
        }
    }

private:
    QDBusConnection m_bus;
    std::function<void(const QString &topic)> m_opened;
    QHash<QString, uint> m_last;
};

// Looks for updates without holding Core up: checkupdates fetches the lists
// into a folder of its own and may wait on the network, then Flatpak's apps,
// then pacman's log for the last full update. One look at a time.
class UpdatesLooker : public QObject
{
public:
    explicit UpdatesLooker(std::function<void(const UpdatesLook &)> seen)
        : m_seen(std::move(seen))
    {
    }

    void look()
    {
        if (m_busy)
            return;
        const QString checker = QStandardPaths::findExecutable(QStringLiteral("checkupdates"));
        if (checker.isEmpty())
            return; // Not an Arch computer: Core has nothing to say about updates.
        m_busy = true;
        m_look = UpdatesLook();
        m_look.at = QDateTime::currentDateTimeUtc();
        run(checker, {}, [this](int code, const QString &output) {
            // 0: updates listed; 2: none; anything else, such as no network: unknown.
            if (code != 0 && code != 2) {
                finish();
                return;
            }
            m_look.known = true;
            m_look.waiting = code == 0 ? countLines(output) : 0;
            const QString flatpak = QStandardPaths::findExecutable(QStringLiteral("flatpak"));
            if (flatpak.isEmpty()) {
                finish();
                return;
            }
            run(flatpak, {QStringLiteral("remote-ls"), QStringLiteral("--updates"), QStringLiteral("--columns=application")},
                [this](int code, const QString &output) {
                    if (code == 0)
                        m_look.waiting += countLines(output);
                    finish();
                });
        });
    }

private:
    void run(const QString &program, const QStringList &arguments, std::function<void(int, const QString &)> done)
    {
        auto *process = new QProcess(this);
        process->setProcessChannelMode(QProcess::SeparateChannels);
        auto *limit = new QTimer(process);
        limit->setSingleShot(true);
        QObject::connect(limit, &QTimer::timeout, process, &QProcess::kill);
        QObject::connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
            process->deleteLater();
            done(status == QProcess::NormalExit ? code : -1, QString::fromUtf8(process->readAllStandardOutput()));
        });
        QObject::connect(process, &QProcess::errorOccurred, this, [process, done](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            process->deleteLater();
            done(-1, QString());
        });
        limit->start(std::chrono::minutes(3));
        process->start(program, arguments);
    }

    void finish()
    {
        if (m_look.known) {
            QFile log(QStringLiteral("/var/log/pacman.log"));
            if (log.open(QIODevice::ReadOnly))
                m_look.lastUpdate = lastFullUpdate(QString::fromUtf8(log.readAll()));
        }
        m_busy = false;
        m_seen(m_look);
    }

    std::function<void(const UpdatesLook &)> m_seen;
    UpdatesLook m_look;
    bool m_busy = false;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("split-rock-core"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Split Rock's front desk: looks at the computer and tells the desktop."));
    parser.addHelpOption();
    const QCommandLineOption folderOption(QStringLiteral("folder"), QStringLiteral("Where the logbook is kept."),
                                          QStringLiteral("dir"));
    const QCommandLineOption settingsOption(QStringLiteral("settings"),
                                            QStringLiteral("The settings file where a topic is switched off."),
                                            QStringLiteral("file"));
    const QCommandLineOption driveOption(QStringLiteral("drive"), QStringLiteral("A folder on the drive to watch."),
                                         QStringLiteral("dir"));
    const QCommandLineOption everyOption(QStringLiteral("every"), QStringLiteral("Minutes between looks at the drive."),
                                         QStringLiteral("minutes"), QStringLiteral("30"));
    const QCommandLineOption updatesOption(QStringLiteral("updates-every"),
                                           QStringLiteral("Hours between looks for updates."), QStringLiteral("hours"),
                                           QStringLiteral("6"));
    parser.addOptions({folderOption, settingsOption, driveOption, everyOption, updatesOption});
    parser.process(app);

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning("Core speaks only on the session bus, and there is none.");
        return 1;
    }
    const QString service = QLatin1String(CoreService::Service);
    if (bus.interface()->isServiceRegistered(service))
        return 0;

    const QString drive = parser.isSet(driveOption) ? parser.value(driveOption) : QDir::homePath();
    const auto openCard = [](const QString &card, const QString &query) {
        QStringList arguments{QStringLiteral("--card"), card};
        if (!query.isEmpty())
            arguments << QStringLiteral("--query") << query;
        QProcess::startDetached(splitRockProgram(), arguments);
    };

    Core *core = nullptr;
    Ticker ticker(bus, [&core](const QString &topic) {
        if (core)
            core->act(topic, QStringLiteral("open"));
    });
    Core::Hooks hooks;
    hooks.look = [drive](const QDateTime &now) { return look(drive, now); };
    hooks.doNotDisturb = [&ticker] { return ticker.doNotDisturb(); };
    hooks.notify = [&ticker](const QString &topic, const QString &summary, const QString &body) {
        ticker.send(topic, summary, body);
    };
    hooks.openCard = openCard;
    Core desk(parser.isSet(folderOption) ? parser.value(folderOption) : Core::defaultFolder(),
              parser.isSet(settingsOption) ? parser.value(settingsOption) : Core::defaultSettings(), hooks);
    core = &desk;

    CoreService offered(&desk);
    if (!offered.publish(bus))
        return 1;
    const auto claimed = bus.interface()->registerService(service, QDBusConnectionInterface::DontQueueService,
                                                          QDBusConnectionInterface::DontAllowReplacement);
    if (!claimed.isValid() || claimed.value() != QDBusConnectionInterface::ServiceRegistered)
        return 0; // Another started at the same moment.

    // The first look once the session has settled, then on the schedule.
    // Looking costs one call to the file system, so it never wakes the
    // computer for long.
    QTimer::singleShot(std::chrono::seconds(20), &desk, [&desk] { desk.check(); });
    QTimer every;
    every.setInterval(std::chrono::minutes(qMax(1, parser.value(everyOption).toInt())));
    QObject::connect(&every, &QTimer::timeout, &desk, [&desk] { desk.check(); });
    every.start();

    // Updates: a first look once the network has had time to come up, then on
    // the schedule, and again a little after an update ends.
    UpdatesLooker updates([&desk](const UpdatesLook &look) { desk.seeUpdates(look); });
    const auto lookForUpdates = [&desk, &updates] {
        if (desk.watchesUpdates())
            updates.look();
    };
    QTimer::singleShot(std::chrono::minutes(2), &desk, lookForUpdates);
    QTimer updatesEvery;
    updatesEvery.setInterval(std::chrono::hours(qMax(1, parser.value(updatesOption).toInt())));
    QObject::connect(&updatesEvery, &QTimer::timeout, &desk, lookForUpdates);
    updatesEvery.start();
    const QString pacmanLog = QStringLiteral("/var/log/pacman.log");
    QFileSystemWatcher logWatcher;
    QTimer settled;
    settled.setSingleShot(true);
    settled.setInterval(std::chrono::minutes(2));
    QObject::connect(&settled, &QTimer::timeout, &desk, lookForUpdates);
    if (QFile::exists(pacmanLog))
        logWatcher.addPath(pacmanLog);
    // Settings, such as how readily Core speaks, take effect when they are
    // written. The folder is watched, since a settings file is replaced, not
    // edited, when it is saved.
    const QString settingsFile = parser.isSet(settingsOption) ? parser.value(settingsOption) : Core::defaultSettings();
    QFileSystemWatcher settingsWatcher({QFileInfo(settingsFile).absolutePath()});
    QDateTime settingsSaved = QFileInfo(settingsFile).lastModified();
    QObject::connect(&settingsWatcher, &QFileSystemWatcher::directoryChanged, &desk,
                     [&desk, settingsFile, &settingsSaved] {
                         const QDateTime saved = QFileInfo(settingsFile).lastModified();
                         if (saved == settingsSaved)
                             return;
                         settingsSaved = saved;
                         desk.reconsider();
                     });
    QObject::connect(&logWatcher, &QFileSystemWatcher::fileChanged, &desk, [&logWatcher, &settled, pacmanLog] {
        // A log turned over is watched afresh.
        if (!logWatcher.files().contains(pacmanLog) && QFile::exists(pacmanLog))
            logWatcher.addPath(pacmanLog);
        settled.start();
    });

    return app.exec();
}

#include "main.moc"
