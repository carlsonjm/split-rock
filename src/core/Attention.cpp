// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Attention.h"

namespace splitrock::core {

Spoken decide(const StorageFact &fact, Attention &state, const QDateTime &now, bool doNotDisturb,
              const StorageLimits &storage, const AttentionLimits &limits)
{
    Spoken spoken;
    if (!fact.known)
        return spoken;

    if (state.islandOpen && state.islandSince.isValid() && state.islandSince.secsTo(now) >= limits.islandHours * 3600) {
        state.islandOpen = false;
        spoken.ignored = true;
    }

    // Settled: well below the ticker's mark and not filling fast. What was
    // said is forgotten, so the next time it fills it speaks again.
    if (fact.level == Level::Quiet && fact.percent < storage.ticker - limits.settle) {
        if (state.spokenLevel != Level::Quiet || state.quietUntil > 0 || state.islandOpen)
            spoken.settled = true;
        const QDate day = state.islandsDay;
        const int today = state.islandsToday;
        state = Attention();
        state.islandsDay = day;
        state.islandsToday = today;
        return spoken;
    }
    if (fact.level == Level::Quiet) {
        state.islandOpen = false;
        return spoken;
    }
    if (state.quietUntil > 0 && fact.percent < state.quietUntil)
        return spoken;

    const bool worse = fact.level > state.spokenLevel
        || (fact.level == state.spokenLevel && fact.percent >= state.spokenPercent + limits.step);
    if (!worse)
        return spoken;

    if (state.islandsDay != now.date()) {
        state.islandsDay = now.date();
        state.islandsToday = 0;
    }
    const bool island = fact.level == Level::Island && !doNotDisturb && state.islandsToday < limits.islandsPerDay;
    if (island) {
        spoken.island = true;
        state.islandOpen = true;
        state.islandSince = now;
        ++state.islandsToday;
    } else {
        spoken.ticker = true;
    }
    state.spokenLevel = fact.level;
    state.spokenPercent = fact.percent;
    state.quietUntil = 0;
    return spoken;
}

bool answer(Attention &state, const QString &action, int percent, const AttentionLimits &limits)
{
    if (action == QLatin1String("open")) {
        state.islandOpen = false;
        return true;
    }
    if (action == QLatin1String("not-now") || action == QLatin1String("dismiss")) {
        state.islandOpen = false;
        state.quietUntil = percent + limits.step;
        return true;
    }
    return false;
}

} // namespace splitrock::core
