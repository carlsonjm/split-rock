// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Storage.h"

#include <QStorageInfo>

#include <cmath>

namespace splitrock::core {

QString levelName(Level level)
{
    switch (level) {
    case Level::Ticker:
        return QStringLiteral("ticker");
    case Level::Island:
        return QStringLiteral("island");
    case Level::Quiet:
        break;
    }
    return QStringLiteral("quiet");
}

QString sizeText(qint64 kb)
{
    constexpr qint64 Gb = 1024 * 1024;
    if (kb >= 10 * Gb)
        return QStringLiteral("%1 GB").arg(qRound64(double(kb) / Gb));
    if (kb >= Gb)
        return QStringLiteral("%1 GB").arg(double(kb) / Gb, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(qRound64(double(kb) / 1024));
}

StorageFact judge(const QList<Sample> &samples, const StorageLimits &limits)
{
    StorageFact fact;
    if (samples.isEmpty() || samples.last().totalKb <= 0)
        return fact;
    const Sample &now = samples.last();
    fact.known = true;
    fact.totalKb = now.totalKb;
    fact.freeKb = qBound<qint64>(0, now.freeKb, now.totalKb);
    const qint64 usedKb = fact.totalKb - fact.freeKb;
    fact.percent = int(std::floor(100.0 * double(usedKb) / double(fact.totalKb)));

    // The trend, from the oldest look of the last week on the same drive.
    const QDateTime weekAgo = now.at.addDays(-7);
    for (const Sample &then : samples) {
        if (then.at < weekAgo || then.totalKb != now.totalKb)
            continue;
        const qint64 seconds = then.at.secsTo(now.at);
        if (seconds < 12 * 3600)
            break;
        fact.trendKnown = true;
        const qint64 grown = usedKb - (then.totalKb - then.freeKb);
        fact.growthKbPerDay = grown > 0 ? qint64(double(grown) * 86400.0 / double(seconds)) : 0;
        break;
    }
    if (fact.growthKbPerDay > 0)
        fact.daysToFull = int(fact.freeKb / fact.growthKbPerDay);

    const bool fast = fact.daysToFull >= 0 && fact.daysToFull <= limits.fastDays && fact.percent >= limits.fastFrom;
    if (fact.percent >= limits.island || fast)
        fact.level = Level::Island;
    else if (fact.percent >= limits.ticker)
        fact.level = Level::Ticker;

    fact.line = QStringLiteral("%1% full").arg(fact.percent);
    fact.detail = sizeText(fact.freeKb) + QStringLiteral(" free");
    // A day's growth under 100 MB is noise from caches coming and going.
    if (fact.growthKbPerDay >= 100 * 1024)
        fact.detail += QStringLiteral(" · filling about %1 a day").arg(sizeText(fact.growthKbPerDay));
    return fact;
}

Sample look(const QString &folder, const QDateTime &now)
{
    Sample sample;
    sample.at = now;
    QStorageInfo drive(folder);
    if (!drive.isValid() || !drive.isReady())
        return sample;
    sample.totalKb = drive.bytesTotal() / 1024;
    sample.freeKb = drive.bytesAvailable() / 1024;
    return sample;
}

} // namespace splitrock::core
