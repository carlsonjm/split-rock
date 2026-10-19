// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Health.h"

#include <QStringList>

namespace splitrock::core {

namespace {

// Storage and Updates are their own topics.
bool othersSpeakFor(const QString &id)
{
    return id == QLatin1String("space") || id == QLatin1String("updates") || id == QLatin1String("pkgcache");
}

} // namespace

HealthFact judge(const HealthLook &look)
{
    HealthFact fact;
    if (!look.known)
        return fact;
    fact.known = true;
    fact.problems = int(look.problems.size());
    for (const HealthProblem &problem : look.problems)
        fact.driveFailing = fact.driveFailing || problem.id == QLatin1String("drive");
    fact.level = fact.driveFailing ? Level::Island : fact.problems > 0 ? Level::Ticker : Level::Quiet;
    if (fact.problems == 0) {
        fact.line = QStringLiteral("All good");
        fact.detail = QStringLiteral("Nothing needs a look");
    } else {
        fact.line = fact.problems == 1 ? QStringLiteral("1 problem") : QStringLiteral("%1 problems").arg(fact.problems);
        // The drive first, since it is the one that can lose files.
        const HealthProblem *first = &look.problems.first();
        for (const HealthProblem &problem : look.problems) {
            if (problem.id == QLatin1String("drive"))
                first = &problem;
        }
        fact.detail = first->title + QStringLiteral(": ") + first->detail;
    }
    return fact;
}

HealthLook readHealthFacts(const QString &output, const QDateTime &at)
{
    HealthLook look;
    look.at = at;
    look.known = true;
    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QStringList cells = line.split(QLatin1Char('\t'));
        if (cells.size() != 4 || cells.at(0) != QLatin1String("problem") || othersSpeakFor(cells.at(1)))
            continue;
        look.problems.append({cells.at(1), cells.at(2), cells.at(3)});
    }
    return look;
}

} // namespace splitrock::core
