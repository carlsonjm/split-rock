// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Fence.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace splitrock {

namespace {

// The desktop's own settings, which the assistant reads to explain the
// computer. Personal files, and settings that can hold another program's
// tokens, stay hidden.
const QStringList DesktopSettings{
    QStringLiteral("kdeglobals"), QStringLiteral("kwinrc"), QStringLiteral("kwinoutputconfig.json"),
    QStringLiteral("plasmashellrc"), QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"),
    QStringLiteral("plasma-localerc"), QStringLiteral("kcminputrc"), QStringLiteral("kxkbrc"),
    QStringLiteral("touchpadxlibinputrc"), QStringLiteral("powerdevilrc"), QStringLiteral("powermanagementprofilesrc"),
    QStringLiteral("bluedevilglobalrc"), QStringLiteral("kscreenlockerrc"), QStringLiteral("ksmserverrc"),
    QStringLiteral("kded6rc"), QStringLiteral("baloofilerc"), QStringLiteral("dolphinrc"), QStringLiteral("katerc"),
    QStringLiteral("mimeapps.list"), QStringLiteral("user-dirs.dirs"), QStringLiteral("user-dirs.locale"),
    QStringLiteral("kwalletrc"), QStringLiteral("plasmanotifyrc"), QStringLiteral("plasma-nm"),
    QStringLiteral("pipewire"), QStringLiteral("wireplumber"), QStringLiteral("pulse/client.conf"),
    QStringLiteral("fontconfig"), QStringLiteral("gtk-3.0/settings.ini"), QStringLiteral("gtk-4.0/settings.ini"),
    QStringLiteral("autostart"), QStringLiteral("environment.d"), QStringLiteral("systemd/user"),
};

} // namespace

static QString writeOpeners(const QString &requests)
{
    if (requests.isEmpty())
        return {};
    const QString folder = QFileInfo(requests).dir().filePath(QStringLiteral("openers"));
    if (!QDir().mkpath(folder))
        return {};
    // Hands the page, the last argument, to Split Rock, which opens it in the
    // person's browser while they are signing in, and otherwise never.
    const QByteArray script =
        "#!/bin/sh\n"
        "for page; do :; done\n"
        "case \"$page\" in http://*|https://*) printf '%s\\n' \"$page\" >> \"$SPLIT_ROCK_OPEN_REQUESTS\" ;; esac\n"
        "exit 0\n";
    for (const QString &name : {QStringLiteral("xdg-open"), QStringLiteral("kde-open"), QStringLiteral("kde-open5"),
                                QStringLiteral("gio"), QStringLiteral("www-browser"), QStringLiteral("x-www-browser"),
                                QStringLiteral("sensible-browser")}) {
        QFile file(QDir(folder).filePath(name));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(script) != script.size())
            return {};
        file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    }
    QFile touch(requests);
    if (!touch.open(QIODevice::Append))
        return {};
    return folder;
}

QString fenceProgram()
{
    return QStandardPaths::findExecutable(QStringLiteral("bwrap"));
}

acp::LaunchSpec fenced(const acp::LaunchSpec &launch, const FenceRoom &room)
{
    const QString home = QDir::homePath();
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");

    QStringList args{
        // The whole system, read-only.
        QStringLiteral("--ro-bind"), QStringLiteral("/"), QStringLiteral("/"),
        QStringLiteral("--dev"), QStringLiteral("/dev"),
        QStringLiteral("--proc"), QStringLiteral("/proc"),
        // Scratch space of its own, gone when it stops.
        QStringLiteral("--tmpfs"), QStringLiteral("/tmp"),
    };
    // Only over folders that exist: the system is read-only, so none can be
    // made for them.
    for (const QString &hidden : {QStringLiteral("/var/tmp"), QStringLiteral("/run/dbus")}) {
        // /run/dbus holds the system's message bus.
        if (QFileInfo(hidden).isDir())
            args << QStringLiteral("--tmpfs") << hidden;
    }
    // The home folder, hidden: an empty folder of its own in its place.
    args << QStringLiteral("--tmpfs") << home;
    // The session's sockets, the bus and the display among them, hidden too.
    if (!runtime.isEmpty() && QFileInfo(runtime).isDir())
        args << QStringLiteral("--tmpfs") << runtime;
    // The desktop's settings, read-only.
    for (const QString &setting : DesktopSettings) {
        const QString path = QDir(config).filePath(setting);
        args << QStringLiteral("--ro-bind-try") << path << path;
    }
    for (const QString &path : room.readable) {
        if (!path.isEmpty())
            args << QStringLiteral("--ro-bind-try") << path << path;
    }
    // What it may write: last, so nothing above covers it.
    for (const QString &path : room.writable) {
        if (path.isEmpty())
            continue;
        QDir().mkpath(path);
        args << QStringLiteral("--bind") << path << path;
    }
    args << QStringLiteral("--unshare-pid") << QStringLiteral("--die-with-parent") << QStringLiteral("--new-session");
    if (!launch.workingDirectory.isEmpty())
        args << QStringLiteral("--chdir") << launch.workingDirectory;
    args << QStringLiteral("--") << launch.program << launch.arguments;
    QProcessEnvironment environment = launch.environment;
    const QString openers = writeOpeners(room.openRequests);
    if (!openers.isEmpty()) {
        // Every way an assistant opens a page finds a stand-in first: the
        // browser variable, xdg-open, and the desktop openers behind it.
        environment.insert(QStringLiteral("PATH"),
                           openers + QLatin1Char(':') + environment.value(QStringLiteral("PATH"), QStringLiteral("/usr/bin:/bin")));
        environment.insert(QStringLiteral("BROWSER"), QDir(openers).filePath(QStringLiteral("xdg-open")));
        environment.insert(QStringLiteral("SPLIT_ROCK_OPEN_REQUESTS"), room.openRequests);
    }
    return {fenceProgram(), args, environment, launch.workingDirectory};
}

} // namespace splitrock
