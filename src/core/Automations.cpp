// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Automations.h"

#include <QSettings>

#include <algorithm>

namespace splitrock::core {

namespace automationWords {

QString offer(const QString &names)
{
    return QStringLiteral("Open %1 when you sign in?").arg(names);
}

QString because(int times)
{
    static const QStringList words{QStringLiteral("three"), QStringLiteral("four"), QStringLiteral("five"),
                                   QStringLiteral("six"),   QStringLiteral("seven"), QStringLiteral("eight"),
                                   QStringLiteral("nine")};
    const QString count = times >= 3 && times <= 9 ? words.at(times - 3) : QString::number(times);
    return QStringLiteral("You've started with these %1 times").arg(count);
}

} // namespace automationWords

namespace {

const QString GroupPrefix = QStringLiteral("Automation ");

} // namespace

QString setKey(const QStringList &apps)
{
    QStringList sorted = apps;
    sorted.sort();
    sorted.removeDuplicates();
    return sorted.join(QLatin1Char('|'));
}

QList<Automation> readAutomations(const QString &settingsFile)
{
    QSettings settings(settingsFile, QSettings::IniFormat);
    QList<std::pair<int, Automation>> found;
    for (const QString &group : settings.childGroups()) {
        if (!group.startsWith(GroupPrefix))
            continue;
        bool number = false;
        const int n = group.mid(GroupPrefix.size()).toInt(&number);
        if (!number)
            continue;
        settings.beginGroup(group);
        Automation automation;
        automation.group = group;
        automation.apps = settings.value(QStringLiteral("Apps")).toStringList();
        automation.names = settings.value(QStringLiteral("Names")).toStringList();
        automation.enabled = settings.value(QStringLiteral("Enabled"), true).toBool();
        automation.added = QDateTime::fromString(settings.value(QStringLiteral("Added")).toString(), Qt::ISODate);
        const QString kind = settings.value(QStringLiteral("Kind"), QStringLiteral("open-together")).toString();
        settings.endGroup();
        // Only the one kind there is so far.
        if (kind == QLatin1String("open-together") && !automation.apps.isEmpty())
            found.append({n, automation});
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QList<Automation> automations;
    for (const auto &[n, automation] : found)
        automations.append(automation);
    return automations;
}

Automation addAutomation(const QString &settingsFile, const QStringList &apps, const QStringList &names,
                         const QDateTime &added)
{
    QSettings settings(settingsFile, QSettings::IniFormat);
    int next = 1;
    for (const QString &group : settings.childGroups()) {
        if (group.startsWith(GroupPrefix))
            next = std::max(next, group.mid(GroupPrefix.size()).toInt() + 1);
    }
    Automation automation{GroupPrefix + QString::number(next), apps, names, true, added};
    settings.beginGroup(automation.group);
    settings.setValue(QStringLiteral("Kind"), QStringLiteral("open-together"));
    settings.setValue(QStringLiteral("Apps"), apps);
    settings.setValue(QStringLiteral("Names"), names);
    settings.setValue(QStringLiteral("Enabled"), true);
    settings.setValue(QStringLiteral("Added"), added.toUTC().toString(Qt::ISODate));
    settings.endGroup();
    settings.sync();
    return automation;
}

std::optional<OpenSet> setToOffer(const QList<OpenSet> &sets, const QStringList &offered,
                                  const QList<Automation> &automations)
{
    QStringList taken = offered;
    for (const Automation &automation : automations)
        taken.append(setKey(automation.apps));
    std::optional<OpenSet> best;
    for (const OpenSet &set : sets) {
        if (set.count < AutomationOfferAfter || set.apps.size() < 2 || taken.contains(setKey(set.apps)))
            continue;
        if (!best || set.count > best->count || (set.count == best->count && set.lastSeen > best->lastSeen))
            best = set;
    }
    return best;
}

} // namespace splitrock::core
