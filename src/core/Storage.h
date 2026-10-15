// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace splitrock::core {

// Storage, Core's first topic (docs/CORE.md): how full the drive that holds
// the person's home folder is, and how fast it is filling. Only read; nothing
// here changes the computer.

// One look at the drive.
struct Sample {
    QDateTime at;
    qint64 totalKb = 0;
    qint64 freeKb = 0;
};

// How loudly a fact may ask for attention: quiet, the ticker, or the island.
enum class Level { Quiet, Ticker, Island };

QString levelName(Level level);

// What Core says about the drive, worked out from its recent looks.
struct StorageFact {
    bool known = false;
    int percent = 0;
    qint64 totalKb = 0;
    qint64 freeKb = 0;
    // How much more is used each day, from the looks of the last week; zero
    // when it is steady or shrinking, and unknown until the looks span half a
    // day.
    bool trendKnown = false;
    qint64 growthKbPerDay = 0;
    // Days until the drive is full at that rate; -1 when it isn't filling.
    int daysToFull = -1;
    Level level = Level::Quiet;
    // "91% full"
    QString line;
    // "18 GB free · filling about 2 GB a day"
    QString detail;
};

// The limits Core holds to. Fixed here, never learned.
struct StorageLimits {
    // The ticker from this full, the island from this full.
    int ticker = 75;
    int island = 90;
    // Filling fast: full within this many days, once at least this full.
    int fastDays = 14;
    int fastFrom = 80;
};

// The fact the looks give, the newest last.
StorageFact judge(const QList<Sample> &samples, const StorageLimits &limits = {});

// The drive holding this folder, looked at now; totalKb is 0 when it can't be
// read.
Sample look(const QString &folder, const QDateTime &now);

// "18 GB", "640 MB".
QString sizeText(qint64 kb);

} // namespace splitrock::core
