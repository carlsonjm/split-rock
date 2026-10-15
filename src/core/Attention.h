// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/Storage.h"

#include <QDate>
#include <QDateTime>

namespace splitrock::core {

// When a fact may speak, and where (docs/CORE.md § How often Core speaks). A
// fact speaks only when it gets worse, never because time passed: once at the
// ticker, once at the island, and again only after it worsens by a set step.
// Not now quiets it until it worsens by that step. The island has a daily
// allowance, and Do Not Disturb leaves only the ticker.
struct Attention {
    Level spokenLevel = Level::Quiet;
    int spokenPercent = 0;
    // Quiet until the fact reaches this; 0 when not quieted.
    int quietUntil = 0;
    bool islandOpen = false;
    QDateTime islandSince;
    QDate islandsDay;
    int islandsToday = 0;
};

struct AttentionLimits {
    // How much worse a fact must get before it speaks again.
    int step = 3;
    // How far below the ticker's mark the fact must fall to count as settled.
    int settle = 2;
    // Islands a day, from all of Core.
    int islandsPerDay = 1;
    // An island nobody touched closes after this long.
    int islandHours = 24;
};

// What one decision says to do now.
struct Spoken {
    bool ticker = false;
    bool island = false;
    // The island closed because nobody touched it.
    bool ignored = false;
    // The fact settled, so what was said is forgotten.
    bool settled = false;
};

Spoken decide(const StorageFact &fact, Attention &state, const QDateTime &now, bool doNotDisturb,
              const StorageLimits &storage = {}, const AttentionLimits &limits = {});

// The person answered the island or the ticker: "open", "not-now" or
// "dismiss". False for any other action.
bool answer(Attention &state, const QString &action, int percent, const AttentionLimits &limits = {});

} // namespace splitrock::core
