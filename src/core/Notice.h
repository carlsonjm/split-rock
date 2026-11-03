// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QSettings>
#include <QString>
#include <QtGlobal>

namespace splitrock::core {

// The person's Notice setting, how readily Core speaks: [General] Notice in
// split-rock-corerc, from 0 (Less) to 100 (More), 50 by default; the words
// quiet and eager are 0 and 100. Header-only, so the Health check reads the
// same marks as Core without linking Core.
inline int noticeLevel(const QString &settingsFile)
{
    const QString word = QSettings(settingsFile, QSettings::IniFormat)
                             .value(QStringLiteral("Notice"), 50).toString().trimmed();
    if (word == QLatin1String("quiet"))
        return 0;
    if (word == QLatin1String("eager"))
        return 100;
    bool number = false;
    const double level = word.toDouble(&number);
    return number ? qBound(0, qRound(level), 100) : 50;
}

// A mark along the Notice slider: the value at 0 (Less), 50 (the default) and
// 100 (More), and straight lines between.
inline int noticeMark(int level, int less, int balanced, int more)
{
    if (level <= 50)
        return qRound(less + (balanced - less) * level / 50.0);
    return qRound(balanced + (more - balanced) * (level - 50) / 50.0);
}

// How full the drive is, in percent, when Core first says so at the ticker,
// and when it takes the island.
inline int storageTickerAt(int level)
{
    return noticeMark(level, 85, 75, 70);
}

inline int storageIslandAt(int level)
{
    return noticeMark(level, 95, 90, 85);
}

} // namespace splitrock::core
