// SPDX-License-Identifier: GPL-2.0-or-later
#include "ask/Fence.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace splitrock::ask {

QString fenceProgram()
{
    return QStandardPaths::findExecutable(QStringLiteral("bwrap"));
}

QStringList fenced(const QString &program, const QStringList &arguments, const FenceRoom &room)
{
    const QString home = QDir::homePath();
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
    // made for them. /run/dbus holds the system's message bus.
    for (const QString &hidden : {QStringLiteral("/var/tmp"), QStringLiteral("/run/dbus")}) {
        if (QFileInfo(hidden).isDir())
            args << QStringLiteral("--tmpfs") << hidden;
    }
    // The home folder, hidden: an empty folder of its own in its place.
    args << QStringLiteral("--tmpfs") << home;
    // The session's sockets, the bus and the display among them, hidden too.
    if (!runtime.isEmpty() && QFileInfo(runtime).isDir())
        args << QStringLiteral("--tmpfs") << runtime;
    for (const QString &path : room.readable) {
        if (path.isEmpty())
            continue;
        // A folder reached through a link, such as /bin on a merged system, is
        // bound where the link leads: bubblewrap won't mount on a link.
        const QString real = QFileInfo(path).canonicalFilePath();
        const QString target = real.isEmpty() ? path : real;
        args << QStringLiteral("--ro-bind-try") << target << target;
    }
    args << QStringLiteral("--unshare-pid") << QStringLiteral("--die-with-parent") << QStringLiteral("--new-session");
    args << QStringLiteral("--") << program << arguments;
    return args;
}

} // namespace splitrock::ask
