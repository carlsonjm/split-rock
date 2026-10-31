// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/WorkspaceMemory.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace splitrock::core {

// Automations, Core's ninth topic (docs/CORE.md § Automations). Once Cards
// has seen the same apps opened together after sign-in three times, Core
// offers once to open them at each sign-in. Nothing is set up without the
// person's yes; each accepted automation is kept in split-rock-corerc with a
// switch of its own, which Settings lists.

// ---------------------------------------------------------------------------
// THE OFFER'S WORDS, naming the apps, as the product owner chose on 30
// October (docs/CORE.md § Automations). Every word the person sees for an offer is
// here, and only here.
namespace automationWords {
// "Open Firefox, Notes and Mail when you sign in?"; the names as namesList()
// gives them, three at most and then "and 2 more".
QString offer(const QString &names);
// "You've started with these three times"
QString because(int times);
// The island's tap, and its other choice.
inline const char *const Yes = "Yes, each time";
inline const char *const NotNow = "Not now";
} // namespace automationWords
// ---------------------------------------------------------------------------

// How many times the same apps must open together before Core offers.
constexpr int AutomationOfferAfter = 3;

// One accepted automation, as kept in the settings file under a group of its
// own, "[Automation 1]": Kind=open-together, Apps (desktop ids), Names,
// Enabled and Added.
struct Automation {
    QString group;
    QStringList apps;
    QStringList names;
    bool enabled = true;
    QDateTime added;
};

// Every automation in that settings file, by its group's number.
QList<Automation> readAutomations(const QString &settingsFile);
// Adds one, switched on; its group is the next number free.
Automation addAutomation(const QString &settingsFile, const QStringList &apps, const QStringList &names,
                         const QDateTime &added);

// "a|b|c": a set of apps, sorted, as one key.
QString setKey(const QStringList &apps);

// The set to offer now, if any: seen at least AutomationOfferAfter times,
// never offered before, and not already an automation. The one seen most,
// then the one seen last.
std::optional<OpenSet> setToOffer(const QList<OpenSet> &sets, const QStringList &offered,
                                  const QList<Automation> &automations);

} // namespace splitrock::core
