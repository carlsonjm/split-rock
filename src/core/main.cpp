// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core, Split Rock's front desk: a background program with no window, one for
// each person, that looks at the computer on a schedule and tells the
// desktop's surfaces when a fact deserves attention (docs/CORE.md). It runs as
// the person, never as the administrator, and never calls a language model.
//
//   split-rock-core [--folder DIR] [--settings FILE] [--drive DIR] [--every MINUTES]
//
// --folder    where its logbook and looks are kept
//             (default: split-rock/core in the person's data folder)
// --settings  the settings file where a topic is switched off
//             (default: split-rock-corerc in the person's settings folder)
// --drive     a folder on the drive to watch (default: the home folder)
// --every     minutes between looks (default: 30)
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
#include <QFileInfo>
#include <QProcess>
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

// Notifications Core sent, by id, so a tap on one opens its card.
class Ticker : public QObject
{
    Q_OBJECT

public:
    Ticker(QDBusConnection bus, std::function<void()> opened)
        : m_bus(bus)
        , m_opened(std::move(opened))
    {
        m_bus.connect(Notifications, NotificationsPath, Notifications, QStringLiteral("ActionInvoked"), this,
                      SLOT(invoked(uint, QString)));
    }

    void send(const QString &summary, const QString &body)
    {
        QDBusMessage call = QDBusMessage::createMethodCall(Notifications, NotificationsPath, Notifications,
                                                           QStringLiteral("Notify"));
        const QVariantMap hints{{QStringLiteral("desktop-entry"), QStringLiteral("io.github.carlsonjm.SplitRock")},
                                {QStringLiteral("urgency"), QVariant::fromValue<uchar>(1)}};
        call.setArguments({QStringLiteral("Storage"), uint(m_last), QStringLiteral("drive-harddisk"), summary, body,
                           QStringList{QStringLiteral("default"), QStringLiteral("Free up space")}, hints, -1});
        const QDBusReply<uint> reply = m_bus.call(call, QDBus::Block, 2000);
        if (reply.isValid())
            m_last = reply.value();
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
        if (id == m_last && m_last != 0 && action == QLatin1String("default"))
            m_opened();
    }

private:
    QDBusConnection m_bus;
    std::function<void()> m_opened;
    uint m_last = 0;
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
    const QCommandLineOption everyOption(QStringLiteral("every"), QStringLiteral("Minutes between looks."),
                                         QStringLiteral("minutes"), QStringLiteral("30"));
    parser.addOptions({folderOption, settingsOption, driveOption, everyOption});
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
    Ticker ticker(bus, [&core] {
        if (core)
            core->act(QStringLiteral("storage"), QStringLiteral("open"));
    });
    Core::Hooks hooks;
    hooks.look = [drive](const QDateTime &now) { return look(drive, now); };
    hooks.doNotDisturb = [&ticker] { return ticker.doNotDisturb(); };
    hooks.notify = [&ticker](const QString &summary, const QString &body) { ticker.send(summary, body); };
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

    return app.exec();
}

#include "main.moc"
