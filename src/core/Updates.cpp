// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Updates.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

namespace splitrock::core {

UpdatesFact judge(const UpdatesLook &look, const QDateTime &waitingSince, const UpdatesLimits &limits)
{
    UpdatesFact fact;
    if (!look.known)
        return fact;
    fact.known = true;
    fact.waiting = qMax(0, look.waiting);
    fact.recorded = look.lastUpdate.isValid();
    const QDateTime since = fact.recorded ? look.lastUpdate : waitingSince;
    const int days = since.isValid() ? int(qMax<qint64>(0, since.secsTo(look.at) / 86400)) : 0;

    if (fact.waiting == 0) {
        fact.line = QStringLiteral("Up to date");
    } else {
        fact.daysBehind = days;
        fact.line = QStringLiteral("%1 waiting").arg(fact.waiting);
        if (days >= limits.island)
            fact.level = Level::Island;
        else if (days >= limits.ticker)
            fact.level = Level::Ticker;
    }

    if (!since.isValid() || (!fact.recorded && fact.waiting == 0))
        fact.detail = QStringLiteral("Checked just now");
    else if (!fact.recorded)
        fact.detail = days == 0 ? QStringLiteral("Waiting since today")
                                : days == 1 ? QStringLiteral("Waiting since yesterday")
                                            : QStringLiteral("Waiting for %1 days").arg(days);
    else
        fact.detail = days == 0 ? QStringLiteral("Last updated today")
                                : days == 1 ? QStringLiteral("Last updated yesterday")
                                            : QStringLiteral("Last updated %1 days ago").arg(days);
    return fact;
}

QString updatesListsFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
        + QStringLiteral("/split-rock/checkup-db");
}

int countLines(const QString &output)
{
    int count = 0;
    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (!line.trimmed().isEmpty())
            ++count;
    }
    return count;
}

QDateTime lastFullUpdate(const QString &pacmanLog)
{
    // [2026-10-01T10:00:00-0500] [PACMAN] starting full system upgrade
    static const QRegularExpression stamp(
        QStringLiteral(R"(^\[(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d)([+-]\d\d)(\d\d)\].*starting full system upgrade)"),
        QRegularExpression::MultilineOption);
    QDateTime last;
    QRegularExpressionMatchIterator matches = stamp.globalMatch(pacmanLog);
    while (matches.hasNext()) {
        const QRegularExpressionMatch m = matches.next();
        last = QDateTime::fromString(m.captured(1) + m.captured(2) + QLatin1Char(':') + m.captured(3), Qt::ISODate);
    }
    return last;
}

} // namespace splitrock::core
