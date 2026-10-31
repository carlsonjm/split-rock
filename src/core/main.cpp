// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core, Split Rock's front desk: a background program with no window, one for
// each person, that looks at the computer on a schedule and tells the
// desktop's surfaces when a fact deserves attention (docs/CORE.md). It runs as
// the person, never as the administrator, and never calls a language model.
//
//   split-rock-core [--folder DIR] [--settings FILE] [--drive DIR] [--every MINUTES]
//                   [--updates-every HOURS] [--downloads DIR] [--desktop DIR]
//
// It also looks over the computer's health once a week, with Split Rock's own
// `split-rock --health-facts`, and, on the system bus, for a printer, a drive
// or a Bluetooth device it hasn't seen before, and in the kernel's own folders
// for a game controller or a screen. It keeps a copy of the
// desktop's settings files, to tell What changed exactly what changed in them
// and since when (src/settings/SettingsHistory.h).
//
// --folder    where its logbook and looks are kept
//             (default: split-rock/core in the person's data folder)
// --settings  the settings file where a topic is switched off
//             (default: split-rock-corerc in the person's settings folder)
// --drive     a folder on the drive to watch (default: the home folder)
// --every     minutes between looks at the drive (default: 30)
// --updates-every
//             hours between looks for updates (default: 6); it also looks a
//             little after pacman's log or a Flatpak installation stops
//             changing, so a finished update is seen at once
// --downloads the folder where a Windows installer would arrive
//             (default: the person's Downloads folder)
// --desktop   the Desktop folder, measured with Downloads for Tidy up
//             Downloads (default: the person's Desktop folder)
//
// Core runs once: a second start ends at once.

#include "core/Core.h"
#include "core/CoreService.h"
#include "settings/SettingsHistory.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QProcess>
#include <QSettings>
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
        // Each topic's name, icon and card.
        struct Words {
            QString name;
            QString icon;
            QString card;
        };
        const Words words = topic == QLatin1String("updates")
            ? Words{QStringLiteral("Updates"), QStringLiteral("system-software-update"), QStringLiteral("Check for updates")}
            : topic == QLatin1String("health")
            ? Words{QStringLiteral("Health"), QStringLiteral("security-high"), QStringLiteral("Health check")}
            : topic == QLatin1String("devices")
            ? Words{QStringLiteral("Devices"), QStringLiteral("device-notifier"), QStringLiteral("Set it up")}
            : topic == QLatin1String("windows")
            ? Words{QStringLiteral("Windows programs"), QStringLiteral("application-x-ms-dos-executable"),
                    QStringLiteral("Find the Linux version")}
            : topic == QLatin1String("tidy")
            ? Words{QStringLiteral("Downloads"), QStringLiteral("folder-download"), QStringLiteral("Tidy up Downloads")}
            : topic == QLatin1String("memory")
            ? Words{QStringLiteral("Workspace Memory"), QStringLiteral("window-restore"), QStringLiteral("Bring back")}
            : Words{QStringLiteral("Storage"), QStringLiteral("drive-harddisk"), QStringLiteral("Free up space")};
        QDBusMessage call = QDBusMessage::createMethodCall(Notifications, NotificationsPath, Notifications,
                                                           QStringLiteral("Notify"));
        const QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("io.github.carlsonjm.SplitRock")},
                                {QStringLiteral("urgency"), QVariant::fromValue<uchar>(1)}};
        // Each topic replaces only its own last notification.
        const uint last = m_last.value(topic);
        call.setArguments({words.name, last, words.icon, summary, body,
                           QStringList{QStringLiteral("default"), words.card}, hints, -1});
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
// into the folder the Check for updates Helper fetches into too, so the
// Helper opens on lists already fresh, and may wait on the network; then
// Flatpak's apps, then pacman's log for the last full update. One look at a
// time.
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
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("CHECKUPDATES_DB"), updatesListsFolder());
        process->setProcessEnvironment(environment);
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

// Looks over the computer's health with Split Rock's own program, which runs
// the Health check's look as the person and only reads. One look at a time.
class HealthLooker : public QObject
{
public:
    explicit HealthLooker(std::function<void(const HealthLook &)> seen)
        : m_seen(std::move(seen))
    {
    }

    void look()
    {
        if (m_process)
            return;
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        auto *limit = new QTimer(m_process);
        limit->setSingleShot(true);
        QObject::connect(limit, &QTimer::timeout, m_process, &QProcess::kill);
        QObject::connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
            const bool known = status == QProcess::NormalExit && code == 0;
            HealthLook seen;
            if (known)
                seen = readHealthFacts(QString::fromUtf8(m_process->readAllStandardOutput()), QDateTime::currentDateTimeUtc());
            m_process->deleteLater();
            m_process = nullptr;
            m_seen(seen);
        });
        QObject::connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            m_process->deleteLater();
            m_process = nullptr;
            m_seen(HealthLook());
        });
        limit->start(std::chrono::minutes(3));
        m_process->start(splitRockProgram(), {QStringLiteral("--health-facts")});
    }

private:
    std::function<void(const HealthLook &)> m_seen;
    QProcess *m_process = nullptr;
};

// What the folders hold, folders inside them too, in kilobytes; links are not
// followed. It stops counting at a hundred thousand files, which is enough to
// say a folder is big.
qint64 heldKb(const QStringList &folders)
{
    qint64 bytes = 0;
    int seen = 0;
    for (const QString &folder : folders) {
        QDirIterator it(folder, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext() && seen < 100000) {
            it.next();
            bytes += it.fileInfo().size();
            ++seen;
        }
    }
    return bytes / 1024;
}

// What a service's ObjectManager lists: each object and its interfaces'
// properties.
using Interfaces = QMap<QString, QVariantMap>;
using ManagedObjects = QMap<QDBusObjectPath, Interfaces>;

// Watches the system bus for devices: UDisks2's drives, BlueZ's paired
// devices and the printers udev hands to systemd's printer.target. Each kind
// is listed whole when Core starts and again a moment after anything about
// it changes, and Core decides what is new. A service that isn't there
// leaves its kind unlooked at.
class DeviceWatcher : public QObject
{
    Q_OBJECT

public:
    DeviceWatcher(QDBusConnection bus, std::function<void(DeviceKind, const QList<Device> &)> seen)
        : m_bus(bus)
        , m_seen(std::move(seen))
    {
        qDBusRegisterMetaType<Interfaces>();
        qDBusRegisterMetaType<ManagedObjects>();
        for (const auto &[kind, timer] : {std::pair{DeviceKind::Drive, &m_drives}, std::pair{DeviceKind::Bluetooth, &m_bluetooth},
                                         std::pair{DeviceKind::Printer, &m_printers}}) {
            timer->setSingleShot(true);
            timer->setInterval(std::chrono::seconds(3));
            QObject::connect(timer, &QTimer::timeout, this, [this, kind = kind] { list(kind); });
        }
        const QString objects = QStringLiteral("org.freedesktop.DBus.ObjectManager");
        for (const QString &signal : {QStringLiteral("InterfacesAdded"), QStringLiteral("InterfacesRemoved")}) {
            m_bus.connect(UDisks, UDisksPath, objects, signal, this, SLOT(drivesChanged()));
            m_bus.connect(BlueZ, QStringLiteral("/"), objects, signal, this, SLOT(bluetoothChanged()));
        }
        // Pairing an existing device changes its Paired property, on its own path.
        m_bus.connect(BlueZ, QString(), QStringLiteral("org.freedesktop.DBus.Properties"),
                      QStringLiteral("PropertiesChanged"), this, SLOT(bluetoothChanged()));
        // systemd speaks of new units only to those who asked.
        m_bus.connect(Systemd, SystemdPath, SystemdManager, QStringLiteral("UnitNew"), this,
                      SLOT(unitNew(QString)));
        m_bus.asyncCall(QDBusMessage::createMethodCall(Systemd, SystemdPath, SystemdManager, QStringLiteral("Subscribe")));
    }

    void start()
    {
        m_drives.start();
        m_bluetooth.start();
        m_printers.start();
    }

private Q_SLOTS:
    void drivesChanged() { m_drives.start(); }
    void bluetoothChanged() { m_bluetooth.start(); }
    void unitNew(const QString &id)
    {
        if (id.endsWith(QLatin1String(".device")))
            m_printers.start();
    }

private:
    const QString UDisks = QStringLiteral("org.freedesktop.UDisks2");
    const QString BlueZ = QStringLiteral("org.bluez");
    const QString Systemd = QStringLiteral("org.freedesktop.systemd1");
    const QString UDisksPath = QStringLiteral("/org/freedesktop/UDisks2");
    const QString SystemdPath = QStringLiteral("/org/freedesktop/systemd1");
    const QString SystemdManager = QStringLiteral("org.freedesktop.systemd1.Manager");

    void list(DeviceKind kind)
    {
        if (kind == DeviceKind::Printer) {
            listPrinters();
            return;
        }
        const bool drives = kind == DeviceKind::Drive;
        const QString service = drives ? UDisks : BlueZ;
        const QString path = drives ? UDisksPath : QStringLiteral("/");
        const QString interface = drives ? QStringLiteral("org.freedesktop.UDisks2.Drive") : QStringLiteral("org.bluez.Device1");
        const QDBusReply<ManagedObjects> reply = m_bus.call(
            QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                           QStringLiteral("GetManagedObjects")),
            QDBus::Block, 3000);
        if (!reply.isValid())
            return;
        QList<Device> present;
        for (const Interfaces &interfaces : reply.value()) {
            if (!interfaces.contains(interface))
                continue;
            const auto device = drives ? driveFrom(interfaces.value(interface)) : bluetoothFrom(interfaces.value(interface));
            if (device)
                present.append(*device);
        }
        m_seen(kind, present);
    }

    QVariant unitProperty(const QDBusObjectPath &unit, const QString &name)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(Systemd, unit.path(),
                                                           QStringLiteral("org.freedesktop.DBus.Properties"),
                                                           QStringLiteral("Get"));
        call.setArguments({QStringLiteral("org.freedesktop.systemd1.Unit"), name});
        const QDBusReply<QDBusVariant> reply = m_bus.call(call, QDBus::Block, 2000);
        return reply.isValid() ? reply.value().variant() : QVariant();
    }

    QDBusObjectPath unit(const QString &method, const QString &name)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(Systemd, SystemdPath, SystemdManager, method);
        call.setArguments({name});
        const QDBusReply<QDBusObjectPath> reply = m_bus.call(call, QDBus::Block, 2000);
        return reply.isValid() ? reply.value() : QDBusObjectPath();
    }

    // The printers are the device units that want printer.target, which
    // udev's own rules give every printer it sees.
    void listPrinters()
    {
        const QDBusObjectPath target = unit(QStringLiteral("LoadUnit"), QStringLiteral("printer.target"));
        if (target.path().isEmpty())
            return;
        QList<Device> present;
        for (const QString &wanting : unitProperty(target, QStringLiteral("WantedBy")).toStringList()) {
            if (!wanting.endsWith(QLatin1String(".device")))
                continue;
            const QDBusObjectPath device = unit(QStringLiteral("GetUnit"), wanting);
            if (device.path().isEmpty())
                continue;
            if (const auto printer = printerFrom(unitProperty(device, QStringLiteral("Description")).toString()))
                present.append(*printer);
        }
        m_seen(DeviceKind::Printer, present);
    }

    QDBusConnection m_bus;
    std::function<void(DeviceKind, const QList<Device> &)> m_seen;
    QTimer m_drives;
    QTimer m_bluetooth;
    QTimer m_printers;
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
    const QCommandLineOption downloadsOption(QStringLiteral("downloads"),
                                             QStringLiteral("Where a Windows installer would arrive."), QStringLiteral("dir"));
    const QCommandLineOption desktopOption(QStringLiteral("desktop"),
                                           QStringLiteral("The Desktop, measured with Downloads."), QStringLiteral("dir"));
    parser.addOptions({folderOption, settingsOption, driveOption, everyOption, updatesOption, downloadsOption, desktopOption});
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
    CoreService *offeredService = nullptr;
    // Cards, from Kadunce, where it runs: Workspace Memory's saved session
    // and the apps opened together.
    KadunceMemory kadunce(bus);
    // And its saved layouts: reading the desk and arranging one.
    KadunceLayouts kadunceLayouts(bus);
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
    hooks.openNotifications = [&offeredService] {
        if (offeredService)
            Q_EMIT offeredService->OpenNotifications();
    };
    // KDE's settings pages are plugins; System Settings opens one by name.
    hooks.hasSettings = [](const QString &page) {
        for (const QString &plugins : QCoreApplication::libraryPaths()) {
            if (QFile::exists(plugins + QStringLiteral("/plasma/kcms/systemsettings/") + page + QStringLiteral(".so")))
                return true;
        }
        return false;
    };
    hooks.openSettings = [](const QString &page) {
        const QString settings = QStandardPaths::findExecutable(QStringLiteral("systemsettings"));
        if (!settings.isEmpty())
            QProcess::startDetached(settings, {page});
        else
            QProcess::startDetached(QStringLiteral("kcmshell6"), {page});
    };
    hooks.bringBack = [&kadunce, &core] {
        kadunce.bringBack([](const QString &id) { launchApp(id); },
                          [&core](const BringBackRun &run) {
                              if (core)
                                  core->broughtBack(run);
                          });
    };
    hooks.dismissSavedSession = [&kadunce] { kadunce.dismissSavedSession(); };
    hooks.openApps = [](const QStringList &ids) {
        for (const QString &id : ids)
            launchApp(id);
    };
    hooks.currentLayout = [&kadunceLayouts](std::function<void(std::optional<CurrentDesk>)> answered) {
        kadunceLayouts.currentLayout(std::move(answered));
    };
    hooks.arrangeLayout = [&kadunceLayouts](const QList<LayoutGroup> &groups, std::function<void(const LayoutPlan &)> armed,
                                            std::function<void(const LayoutRun &)> done) {
        kadunceLayouts.arrange(groups, std::move(armed), std::move(done));
    };
    Core desk(parser.isSet(folderOption) ? parser.value(folderOption) : Core::defaultFolder(),
              parser.isSet(settingsOption) ? parser.value(settingsOption) : Core::defaultSettings(), hooks);
    core = &desk;

    CoreService offered(&desk);
    offeredService = &offered;
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
    // What changes when an update finishes: pacman's log, and the mark each
    // Flatpak installation, the system's and the person's own, sets on every
    // install or update. A mark is replaced, not edited, so its folder is
    // watched too, and the mark again whenever it comes back.
    const QStringList flatpaks{QStringLiteral("/var/lib/flatpak"),
                               QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                                   + QStringLiteral("/flatpak")};
    QStringList marks{QStringLiteral("/var/log/pacman.log")};
    for (const QString &flatpak : flatpaks)
        marks.append(flatpak + QStringLiteral("/.changed"));
    QFileSystemWatcher logWatcher;
    QTimer settled;
    settled.setSingleShot(true);
    settled.setInterval(std::chrono::minutes(2));
    QObject::connect(&settled, &QTimer::timeout, &desk, lookForUpdates);
    const auto watchMarks = [&logWatcher, marks, flatpaks] {
        for (const QString &mark : marks) {
            if (!logWatcher.files().contains(mark) && QFile::exists(mark))
                logWatcher.addPath(mark);
        }
        for (const QString &flatpak : flatpaks) {
            if (!logWatcher.directories().contains(flatpak) && QFileInfo(flatpak).isDir())
                logWatcher.addPath(flatpak);
        }
    };
    watchMarks();
    // Health: once a week, counted from the last look, so a computer that was
    // off or asleep looks soon after it is back. Asking each hour costs
    // nothing until the week is up.
    HealthLooker health([&desk](const HealthLook &look) { desk.seeHealth(look); });
    const auto lookAtHealth = [&desk, &health] {
        if (desk.healthDue())
            health.look();
    };
    QTimer::singleShot(std::chrono::minutes(10), &desk, lookAtHealth);
    QTimer healthEvery;
    healthEvery.setInterval(std::chrono::hours(1));
    QObject::connect(&healthEvery, &QTimer::timeout, &desk, lookAtHealth);
    healthEvery.start();

    // Windows installers: a new .exe or .msi in the Downloads folder, a few
    // seconds after the folder stops changing, so a download has finished
    // and been named. Only files that arrive while Core runs count; one
    // already there was seen before, or arrived when nobody asked.
    const QString downloads = parser.isSet(downloadsOption)
        ? parser.value(downloadsOption)
        : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QDateTime started = QDateTime::currentDateTime();
    QSet<QString> installersSeen;
    QFileSystemWatcher downloadsWatcher;
    if (!downloads.isEmpty() && QFileInfo(downloads).isDir())
        downloadsWatcher.addPath(downloads);
    QTimer downloadsSettled;
    downloadsSettled.setSingleShot(true);
    downloadsSettled.setInterval(std::chrono::seconds(5));
    QObject::connect(&downloadsWatcher, &QFileSystemWatcher::directoryChanged, &desk, [&downloadsSettled] {
        downloadsSettled.start();
    });
    QObject::connect(&downloadsSettled, &QTimer::timeout, &desk, [&desk, downloads, started, &installersSeen] {
        if (!desk.watchesDownloads())
            return;
        const QFileInfoList files = QDir(downloads).entryInfoList({QStringLiteral("*.exe"), QStringLiteral("*.msi"),
                                                                   QStringLiteral("*.EXE"), QStringLiteral("*.MSI")},
                                                                  QDir::Files, QDir::Time);
        for (const QFileInfo &file : files) {
            if (file.lastModified() < started || installersSeen.contains(file.fileName()))
                continue;
            installersSeen.insert(file.fileName());
            desk.seeInstaller(file.fileName());
        }
    });

    // Tidy up Downloads: what Downloads and the Desktop hold, with each look at
    // the drive and when a download has settled.
    QStringList tidied;
    const QString desktop = parser.isSet(desktopOption) ? parser.value(desktopOption)
                                                        : QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    for (const QString &folder : {downloads, desktop}) {
        if (!folder.isEmpty() && QFileInfo(folder).isDir() && QDir(folder) != QDir::home() && !tidied.contains(folder))
            tidied.append(folder);
    }
    const auto measureTidy = [&desk, tidied] {
        if (desk.watchesTidy() && !tidied.isEmpty())
            desk.seeTidy(heldKb(tidied));
    };
    QTimer::singleShot(std::chrono::minutes(5), &desk, measureTidy);
    QObject::connect(&every, &QTimer::timeout, &desk, measureTidy);
    QObject::connect(&downloadsSettled, &QTimer::timeout, &desk, measureTidy);

    // The desktop's settings: compared with Core's copies a few seconds after
    // a file stops changing, and with each look at the drive. A settings
    // file is replaced when written, not edited, so the folder is watched,
    // and each file again whenever it comes back.
    const QString settingsFolder = parser.isSet(folderOption) ? parser.value(folderOption) + QStringLiteral("/settings")
                                                              : splitrock::SettingsHistory::defaultFolder();
    splitrock::SettingsHistory settingsHistory(settingsFolder,
                                               QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    QFileSystemWatcher desktopWatcher;
    const QString coreSettings = parser.isSet(settingsOption) ? parser.value(settingsOption) : Core::defaultSettings();
    QTimer settingsSettled;
    settingsSettled.setSingleShot(true);
    settingsSettled.setInterval(std::chrono::seconds(5));
        const auto watchSettings = [&desktopWatcher, &settingsHistory] {
        if (!desktopWatcher.directories().contains(settingsHistory.configHome()) && QFileInfo(settingsHistory.configHome()).isDir())
            desktopWatcher.addPath(settingsHistory.configHome());
        for (const QString &file : splitrock::SettingsHistory::watched()) {
            const QString path = settingsHistory.pathOf(file);
            if (!desktopWatcher.files().contains(path) && QFile::exists(path))
                desktopWatcher.addPath(path);
        }
    };
    const auto compareSettings = [&settingsHistory, &watchSettings, coreSettings] {
        // Core switched off as a whole keeps no copies either.
        if (!QSettings(coreSettings, QSettings::IniFormat).value(QStringLiteral("Enabled"), true).toBool())
            return;
        const auto changed = settingsHistory.look(QDateTime::currentDateTime());
        if (!changed.isEmpty())
            qInfo().noquote() << QStringLiteral("Core: %1 settings changed").arg(changed.size());
        watchSettings();
    };
    QObject::connect(&desktopWatcher, &QFileSystemWatcher::directoryChanged, &desk, [&settingsSettled] { settingsSettled.start(); });
    QObject::connect(&desktopWatcher, &QFileSystemWatcher::fileChanged, &desk, [&settingsSettled] { settingsSettled.start(); });
    QObject::connect(&settingsSettled, &QTimer::timeout, &desk, compareSettings);
    QObject::connect(&every, &QTimer::timeout, &desk, compareSettings);
    QTimer::singleShot(std::chrono::seconds(15), &desk, compareSettings);

    // Devices: a first list of each kind once the session has settled, then
    // whenever the system bus says something about them changed.
    DeviceWatcher devices(QDBusConnection::systemBus(), [&desk](DeviceKind kind, const QList<Device> &present) {
        desk.seeDevices(kind, present);
    });
    QTimer::singleShot(std::chrono::seconds(30), &desk, [&devices, &desk] {
        if (desk.watchesDevices())
            devices.start();
    });
    // Game controllers and screens are read from the kernel's own folders,
    // which cost a few small reads: when a node comes or goes in /dev/input,
    // and every half minute, since a screen plugged in says so nowhere Core
    // can watch without the desktop's own display service.
    const auto listLocal = [&desk] {
        if (!desk.watchesDevices())
            return;
        desk.seeDevices(DeviceKind::Controller, controllersIn());
        desk.seeDevices(DeviceKind::Screen, screensIn());
    };
    QFileSystemWatcher inputWatcher;
    if (QFileInfo(QStringLiteral("/dev/input")).isDir())
        inputWatcher.addPath(QStringLiteral("/dev/input"));
    QTimer inputSettled;
    inputSettled.setSingleShot(true);
    inputSettled.setInterval(std::chrono::seconds(3));
    QObject::connect(&inputWatcher, &QFileSystemWatcher::directoryChanged, &desk, [&inputSettled] { inputSettled.start(); });
    QObject::connect(&inputSettled, &QTimer::timeout, &desk, listLocal);
    QTimer localEvery;
    localEvery.setInterval(std::chrono::seconds(30));
    QObject::connect(&localEvery, &QTimer::timeout, &desk, listLocal);
    QTimer::singleShot(std::chrono::seconds(30), &desk, [&localEvery, listLocal] {
        listLocal();
        localEvery.start();
    });

    // Workspace Memory: after sign-in, once Cards has had time to start and
    // read what it saved, the session it offers. Asked three times at most,
    // until Cards answers; a session already offered is not offered again.
    // Then the automations the person accepted open, but for the apps
    // Always is bringing back, and the layouts switched on for sign-in open
    // once the question is settled.
    int memoryAsks = 0;
    bool memoryAnswered = false;
    bool automationsRan = false;
    QTimer memoryAsk;
    memoryAsk.setInterval(std::chrono::seconds(40));
    const auto runAutomations = [&desk, &automationsRan](const QStringList &skip) {
        if (automationsRan)
            return;
        automationsRan = true;
        desk.runAutomations(skip);
        desk.openLayoutsAtSignIn();
    };
    const auto askMemory = [&desk, &kadunce, &memoryAsks, &memoryAnswered, &memoryAsk, runAutomations] {
        if (memoryAnswered)
            return;
        if (++memoryAsks > 3 || !desk.watchesMemory()) {
            memoryAsk.stop();
            runAutomations({});
            return;
        }
        kadunce.savedSession([&desk, &memoryAnswered, &memoryAsk, runAutomations](const std::optional<SavedSession> &session) {
            if (!session || memoryAnswered)
                return;
            memoryAnswered = true;
            memoryAsk.stop();
            const bool comingBack = session->available && desk.bringsBackAlways();
            desk.seeSavedSession(*session);
            runAutomations(comingBack ? session->apps : QStringList());
        });
    };
    QObject::connect(&memoryAsk, &QTimer::timeout, &desk, askMemory);
    QTimer::singleShot(std::chrono::seconds(20), &desk, [&memoryAsk, askMemory] {
        askMemory();
        memoryAsk.start();
    });

    // Automations: which apps were opened together after sign-in, once Cards
    // has had its five minutes to see them, then every two hours.
    const auto lookTogether = [&desk, &kadunce] {
        if (!desk.watchesAutomations())
            return;
        kadunce.openTogether([&desk](const std::optional<QList<OpenSet>> &sets) {
            if (sets)
                desk.seeOpenTogether(*sets);
        });
    };
    QTimer togetherEvery;
    togetherEvery.setInterval(std::chrono::hours(2));
    QObject::connect(&togetherEvery, &QTimer::timeout, &desk, lookTogether);
    QTimer::singleShot(std::chrono::minutes(7), &desk, [&togetherEvery, lookTogether] {
        lookTogether();
        togetherEvery.start();
    });

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
    // An update ending is seen two minutes after the last change.
    const auto changed = [&settled, watchMarks] {
        watchMarks();
        settled.start();
    };
    QObject::connect(&logWatcher, &QFileSystemWatcher::fileChanged, &desk, changed);
    QObject::connect(&logWatcher, &QFileSystemWatcher::directoryChanged, &desk, changed);

    return app.exec();
}

#include "main.moc"
