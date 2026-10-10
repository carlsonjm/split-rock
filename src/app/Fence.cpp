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

// The split-rock-change stand-in: the assistant's one way to propose a change.
// It hands the proposal to Split Rock, which shows it on the Do it card; the
// fence itself changes nothing.
static bool writeProposer(const QString &folder, const QString &requests)
{
    const QByteArray script =
        "#!/bin/sh\n"
        "if [ $# -ne 1 ]; then\n"
        "  echo 'usage: split-rock-change <proposal as one JSON argument>' >&2\n"
        "  exit 2\n"
        "fi\n"
        "printf '%s' \"$1\" | base64 -w0 >> \"$SPLIT_ROCK_CHANGE_REQUESTS\" && echo >> \"$SPLIT_ROCK_CHANGE_REQUESTS\" || exit 1\n"
        "echo 'Proposed. The person decides on the Do it card; the change is made only if they tap Do it, and you will be told how it went.'\n";
    QFile file(QDir(folder).filePath(QStringLiteral("split-rock-change")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(script) != script.size())
        return false;
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile touch(requests);
    return touch.open(QIODevice::Append);
}

// The split-rock-skill stand-in: the assistant's way to open a settings skill.
// Split Rock answers with the skill and what its look-around commands read on
// this computer, outside the fence, which the fence itself cannot see.
static bool writeSkillOpener(const QString &folder, const QString &requests)
{
    const QByteArray script =
        "#!/bin/sh\n"
        "case \"$1\" in ''|*[!a-z0-9-]*) echo 'usage: split-rock-skill <id from the shelf>' >&2; exit 2 ;; esac\n"
        "token=\"$$.$(date +%s%N)\"\n"
        "answer=\"$(dirname \"$SPLIT_ROCK_SKILL_REQUESTS\")/skill-$token.out\"\n"
        "printf '%s %s\\n' \"$token\" \"$1\" >> \"$SPLIT_ROCK_SKILL_REQUESTS\" || exit 1\n"
        "tries=0\n"
        "while [ ! -f \"$answer\" ]; do\n"
        "  tries=$((tries + 1))\n"
        "  if [ $tries -gt 150 ]; then echo 'The skill did not open. Carry on without it.' >&2; exit 1; fi\n"
        "  sleep 0.1\n"
        "done\n"
        "cat \"$answer\"\n"
        "rm -f \"$answer\"\n";
    QFile file(QDir(folder).filePath(QStringLiteral("split-rock-skill")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(script) != script.size())
        return false;
    file.close();
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile touch(requests);
    return touch.open(QIODevice::Append);
}

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
    for (const QString &device : room.devices) {
        if (!device.isEmpty())
            args << QStringLiteral("--dev-bind-try") << device << device;
    }
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
        if (path.isEmpty())
            continue;
        // A folder reached through a link, such as /bin on a merged system, is
        // bound where the link leads: bubblewrap won't mount on a link.
        const QString real = QFileInfo(path).canonicalFilePath();
        const QString target = real.isEmpty() ? path : real;
        args << QStringLiteral("--ro-bind-try") << target << target;
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
        if (!room.changeRequests.isEmpty() && writeProposer(openers, room.changeRequests))
            environment.insert(QStringLiteral("SPLIT_ROCK_CHANGE_REQUESTS"), room.changeRequests);
        if (!room.skillRequests.isEmpty() && writeSkillOpener(openers, room.skillRequests))
            environment.insert(QStringLiteral("SPLIT_ROCK_SKILL_REQUESTS"), room.skillRequests);
    }
    return {fenceProgram(), args, environment, launch.workingDirectory};
}

} // namespace splitrock
