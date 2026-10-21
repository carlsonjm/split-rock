// SPDX-License-Identifier: GPL-2.0-or-later
#include "ask/Fence.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace splitrock::ask {

namespace {

// Bound read-only where the link leads, since bubblewrap won't mount on a
// link: /bin on a merged system, or resolv.conf kept under /run.
void bindReadOnly(QStringList &args, const QString &path)
{
    if (path.isEmpty())
        return;
    const QString real = QFileInfo(path).canonicalFilePath();
    const QString target = real.isEmpty() ? path : real;
    args << QStringLiteral("--ro-bind-try") << target << target;
}

} // namespace

QString fenceProgram()
{
    return QStandardPaths::findExecutable(QStringLiteral("bwrap"));
}

QStringList fenced(const QString &program, const QStringList &arguments, const FenceRoom &room)
{
    const QString home = QDir::homePath();

    // An empty root of its own, with only what is named here in it.
    QStringList args;
    // The programs and the system's own settings, read-only.
    args << QStringLiteral("--ro-bind") << QStringLiteral("/usr") << QStringLiteral("/usr");
    for (const QString &top : {QStringLiteral("/bin"), QStringLiteral("/sbin"), QStringLiteral("/lib"),
                               QStringLiteral("/lib64"), QStringLiteral("/lib32")}) {
        const QFileInfo info(top);
        if (info.isSymLink())
            args << QStringLiteral("--symlink") << info.symLinkTarget() << top;
        else if (info.isDir())
            args << QStringLiteral("--ro-bind") << top << top;
    }
    for (const QString &system : {QStringLiteral("/etc"), QStringLiteral("/opt"), QStringLiteral("/sys"),
                                  // What is installed, for questions about apps.
                                  QStringLiteral("/var/lib/pacman"), QStringLiteral("/var/lib/flatpak")})
        args << QStringLiteral("--ro-bind-try") << system << system;
    args << QStringLiteral("--dev") << QStringLiteral("/dev")
         << QStringLiteral("--proc") << QStringLiteral("/proc")
         // Scratch space of its own, gone when it stops.
         << QStringLiteral("--tmpfs") << QStringLiteral("/tmp")
         << QStringLiteral("--tmpfs") << QStringLiteral("/run");
    // The name server, where the system keeps it under /run.
    const QString resolver = QFileInfo(QStringLiteral("/etc/resolv.conf")).canonicalFilePath();
    if (resolver.startsWith(QLatin1String("/run/")))
        bindReadOnly(args, QFileInfo(resolver).absolutePath());
    for (const QString &device : room.devices) {
        if (!device.isEmpty())
            args << QStringLiteral("--dev-bind-try") << device << device;
    }
    // The home folder, empty but for what it is given.
    args << QStringLiteral("--dir") << home;
    for (const QString &path : room.readable)
        bindReadOnly(args, path);
    // What it may write: last, so nothing above covers it.
    for (const QString &path : room.writable) {
        if (path.isEmpty())
            continue;
        QDir().mkpath(path);
        args << QStringLiteral("--bind") << path << path;
    }
    args << QStringLiteral("--unshare-pid") << QStringLiteral("--unshare-ipc")
         << QStringLiteral("--die-with-parent") << QStringLiteral("--new-session");
    args << QStringLiteral("--") << program << arguments;
    return args;
}

QProcessEnvironment fencedEnvironment(const QProcessEnvironment &base)
{
    // Only these, by name: an allowed list, since a list of what to drop
    // misses the next program's token.
    static const QRegularExpression kept(QStringLiteral(
        "^(PATH|HOME|USER|LOGNAME|LANG|LANGUAGE|LC_[A-Z]+|TZ"
        "|XDG_(CONFIG|DATA|CACHE|STATE)_HOME"
        "|(HTTPS?|ALL|NO)_PROXY|(https?|all|no)_proxy|SSL_CERT_(FILE|DIR))$"));
    QProcessEnvironment env;
    for (const QString &key : base.keys()) {
        if (kept.match(key).hasMatch())
            env.insert(key, base.value(key));
    }
    if (!env.contains(QStringLiteral("PATH")))
        env.insert(QStringLiteral("PATH"), QStringLiteral("/usr/local/bin:/usr/bin:/bin"));
    return env;
}

} // namespace splitrock::ask
