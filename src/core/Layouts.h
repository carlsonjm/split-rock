// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace splitrock::core {

// Saved layouts, Core's side (docs/CORE.md § Layouts). A layout is a named
// desk: groups of apps on one workspace, each group one app standing alone or
// a Stack. Core keeps them in split-rock-corerc, one group each, and opens
// one by asking Cards, from Kadunce, to arrange it and launching what Cards
// names. The calls are Kadunce's layout contract, version 1, on the session
// bus at org.kde.KWin, /Kadunce, co.goodinput.Kadunce.

// One group of a layout: desktop file ids without .desktop, in Stack order,
// front first; stack is true when two or more of them share a Stack.
struct LayoutGroup {
    QStringList apps;
    bool stack = false;
    bool operator==(const LayoutGroup &other) const { return apps == other.apps && stack == other.stack; }
};

// One saved layout, as kept under "[Layout 1]": Name, Groups (the JSON array
// of {apps, stack}), AtSignIn and Added. Its id is the group's number.
struct Layout {
    QString id;
    QString name;
    QList<LayoutGroup> groups;
    bool atSignIn = false;
    QDateTime added;
};

// Groups made tidy: an id with ".desktop" loses it, an id that could name a
// path is dropped, an app appears once, in its first group, a group left
// empty goes, and a group of one app is no Stack.
QList<LayoutGroup> tidyGroups(const QList<LayoutGroup> &groups);
// The JSON array of {apps, stack}, read and written; extra keys are ignored.
QList<LayoutGroup> groupsFromJson(const QString &json);
QString groupsToJson(const QList<LayoutGroup> &groups);
// Groups as they come off the bus, aa{sv} of apps and stack, and back.
QList<LayoutGroup> groupsFrom(const QVariantList &answer);
QList<QVariantMap> groupsToBus(const QList<LayoutGroup> &groups);

// Every layout in that settings file, by its group's number.
QList<Layout> readLayouts(const QString &settingsFile);
// Saves one; a layout of the same name, whatever its case, is replaced and
// keeps its id. Nothing is saved, and the id is empty, for an empty name or
// no groups.
Layout saveLayout(const QString &settingsFile, const QString &name, const QList<LayoutGroup> &groups, bool atSignIn,
                  const QDateTime &added);
bool removeLayout(const QString &settingsFile, const QString &id);
bool setLayoutAtSignIn(const QString &settingsFile, const QString &id, bool on);

// The desk Cards reads now: currentLayout().
struct CurrentDesk {
    QString workspace;
    QList<LayoutGroup> groups;
};
CurrentDesk currentDeskFrom(const QVariantMap &answer);

// What arrangeLayout(token, groups) armed.
struct LayoutPlan {
    bool accepted = false;
    QStringList launch;
    QStringList alreadyOpen;
    // "busy" or "empty" when not accepted.
    QString reason;
};
LayoutPlan layoutPlanFrom(const QVariantMap &answer);

// How an arrangement went.
struct LayoutRun {
    bool accepted = false;
    QString reason;
    QStringList launch;
    QStringList alreadyOpen;
    int placed = 0;
    int missed = 0;
    // The apps that didn't reach their place, by desktop id.
    QStringList missedApps;
    // False when Cards never said the run was over, and Core stopped waiting.
    bool finished = false;
};

// Kadunce's layout calls, asked without waiting: an answer comes back to its
// callback, and nothing when Kadunce isn't there or speaks a version Core
// doesn't know. A Kadunce that isn't running is never started for it.
class KadunceLayouts : public QObject
{
    Q_OBJECT

public:
    static constexpr int Version = 1;
    // How long Core waits after the last word from Cards before it stops
    // waiting for a run to end.
    static constexpr int RunLimitSeconds = 90;

    explicit KadunceLayouts(QDBusConnection bus, const QString &service = QStringLiteral("org.kde.KWin"),
                            QObject *parent = nullptr);

    void currentLayout(std::function<void(std::optional<CurrentDesk>)> done);
    // Arms the arrangement; armed hears what Cards accepted, so the apps it
    // names can be launched, and done hears how the run went: at once when
    // Cards turned it down, otherwise when Cards says it is over, or after
    // RunLimitSeconds without a word.
    void arrange(const QList<LayoutGroup> &groups, std::function<void(const LayoutPlan &)> armed,
                 std::function<void(const LayoutRun &)> done);
    bool running() const { return !m_token.isEmpty(); }

private Q_SLOTS:
    void layoutAppPlaced(const QString &token, const QString &appId, bool placed);
    void layoutFinished(const QString &token, int placed, int missed);

private:
    void whenKnown(std::function<void(bool)> next);
    void finish(bool finished);

    QDBusConnection m_bus;
    QString m_service;
    int m_version = 0;
    QString m_token;
    LayoutRun m_run;
    QSet<QString> m_placed;
    std::function<void(const LayoutRun &)> m_done;
    QTimer m_limit;
};

} // namespace splitrock::core
