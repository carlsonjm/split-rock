// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/Storage.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace splitrock::core {

// Health, Core's third topic (docs/CORE.md): the problems the Health check
// finds, looked for once a week with `split-rock --health-facts`, which runs
// the card's own look and only reads. Space, updates and old copies of
// updates are left out: Storage and Updates speak for them.

struct HealthProblem {
    QString id;
    QString title;
    QString detail;
};

// One look.
struct HealthLook {
    QDateTime at;
    // False when the look failed: the last fact stands.
    bool known = false;
    QList<HealthProblem> problems;
};

struct HealthFact {
    bool known = false;
    int problems = 0;
    // The drive reports it is failing: the one problem that can lose files.
    bool driveFailing = false;
    Level level = Level::Quiet;
    // "1 problem", "All good"
    QString line;
    // The first problem's own words, or "Nothing needs a look"
    QString detail;
};

// The fact a look gives: the ticker for any problem, the island only for a
// failing drive.
HealthFact judge(const HealthLook &look);

// What `split-rock --health-facts` printed, one item to a line: its state
// (good, look or problem), id, title and detail, separated by tabs. Only the
// problems are kept.
HealthLook readHealthFacts(const QString &output, const QDateTime &at);

// How often Core looks.
constexpr int HealthEveryDays = 7;

} // namespace splitrock::core
