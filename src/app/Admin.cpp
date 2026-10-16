// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Admin.h"

#include <QDir>
#include <QFileInfo>

namespace splitrock {

QString adminHelper()
{
    return QStringLiteral(SPLIT_ROCK_ADMIN_HELPER);
}

bool adminTrusted(const QString &program, QString *why)
{
    const auto fail = [why](const QString &reason) {
        if (why)
            *why = reason;
        return false;
    };
    const QFileInfo named(program);
    if (!named.isAbsolute())
        return fail(QStringLiteral("%1 is not a full path").arg(program));
    const QString real = named.canonicalFilePath();
    if (real.isEmpty() || !QFileInfo(real).isFile() || !QFileInfo(real).isExecutable())
        return fail(QStringLiteral("%1 is not installed").arg(program));
    constexpr QFileDevice::Permissions othersWrite = QFileDevice::WriteGroup | QFileDevice::WriteOther;
    for (QString path = real;; path = QFileInfo(path).path()) {
        const QFileInfo at(path);
        if (at.ownerId() != 0 || (at.permissions() & othersWrite))
            return fail(QStringLiteral("%1 can be changed by someone other than the administrator").arg(path));
        if (at.isRoot() || path == QLatin1String("/"))
            break;
    }
    return true;
}

QStringList adminJob(const QStringList &elevate, const QStringList &helper, const QString &job,
                     const QStringList &arguments, const QString &logs, QString *why)
{
    if (helper.isEmpty())
        return {};
    if (!elevate.isEmpty())
        return adminTrusted(helper.first(), why) ? elevate + helper + QStringList{job} + arguments : QStringList{};
    QStringList program{QStringLiteral("env")};
    if (!logs.isEmpty())
        program << QStringLiteral("SPLIT_ROCK_ADMIN_LOGS=") + logs;
    return program + helper + QStringList{job} + arguments;
}

} // namespace splitrock
