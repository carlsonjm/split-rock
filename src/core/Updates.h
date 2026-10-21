// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/Storage.h"

#include <QDateTime>
#include <QString>

namespace splitrock::core {

// Updates, Core's second topic (docs/CORE.md): how many updates are waiting
// and how long it has been since the last full update. Only read; checking
// fetches the package lists into a folder of their own, never the system's,
// and nothing here installs anything.

// One look for updates.
struct UpdatesLook {
    QDateTime at;
    // False when the look failed, such as with no network: the last fact stands.
    bool known = false;
    // System packages and Flatpak apps waiting.
    int waiting = 0;
    // When the last full update started; invalid when the computer has no record.
    QDateTime lastUpdate;
};

struct UpdatesFact {
    bool known = false;
    int waiting = 0;
    // Days since the last full update, or since Core first saw updates
    // waiting where the computer keeps no record; 0 when nothing waits.
    int daysBehind = 0;
    // Whether lastUpdate was known, which decides how the detail reads.
    bool recorded = false;
    Level level = Level::Quiet;
    // "12 waiting"
    QString line;
    // "Last updated 9 days ago"
    QString detail;
};

// How many days behind, with updates waiting, before the ticker and the island.
struct UpdatesLimits {
    int ticker = 7;
    int island = 21;
};

// The fact a look gives. waitingSince is when Core first saw updates waiting
// without a break, for a computer that keeps no record of its updates.
UpdatesFact judge(const UpdatesLook &look, const QDateTime &waitingSince, const UpdatesLimits &limits = {});

// Where checkupdates keeps the package lists it fetches, for Core's look and
// the Check for updates Helper alike (app/SystemUpdate.cpp names the same
// folder): a fetch by either leaves the other only what changed since, and the
// folder outlives a restart, which checkupdates' own in /tmp does not.
QString updatesListsFolder();

// The updates a checker listed, one to a line; blank lines don't count.
int countLines(const QString &output);

// When the last full update started, from pacman's log; invalid when there is none.
QDateTime lastFullUpdate(const QString &pacmanLog);

} // namespace splitrock::core
