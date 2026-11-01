// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QList>
#include <QObject>
#include <QVariantMap>

namespace splitrock::core {

class Core;

// Core's facts, offered on the session bus to the desktop's surfaces
// (docs/CORE.md § On the bus). A surface that finds nothing here carries on as
// it always does.
class CoreService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.SplitRock.Core")

public:
    static constexpr uint Version = 1;
    static constexpr char Service[] = "io.github.carlsonjm.splitrock.Core";
    static constexpr char Path[] = "/Core";

    explicit CoreService(Core *core, QObject *parent = nullptr);

    // Registers the object on the bus; the name is the program's to claim.
    bool publish(QDBusConnection bus);

public Q_SLOTS:
    Q_SCRIPTABLE uint ProtocolVersion() const;
    Q_SCRIPTABLE QList<QVariantMap> Facts() const;
    Q_SCRIPTABLE bool Act(const QString &topic, const QString &action);
    Q_SCRIPTABLE QList<QVariantMap> Actions() const;
    Q_SCRIPTABLE bool OpenAction(const QString &id, const QString &query);
    Q_SCRIPTABLE bool RemoveApp(const QString &desktopId);
    // The desktop's notifications say Do Not Disturb ended, and how many
    // notifications came in meanwhile.
    Q_SCRIPTABLE void FocusEnded(uint notifications);
    // The same, ended inside the notifications, where the person already
    // sees what came in: nothing goes on the island.
    Q_SCRIPTABLE void FocusEndedInView(uint notifications);
    // The person opened the notifications; the island about what came in
    // goes.
    Q_SCRIPTABLE void NotificationsSeen();

    // Saved layouts (docs/CORE.md § Layouts). CurrentLayout and OpenLayout
    // answer once Cards has.
    Q_SCRIPTABLE QList<QVariantMap> Layouts() const;
    Q_SCRIPTABLE QVariantMap CurrentLayout();
    Q_SCRIPTABLE QString SaveLayout(const QString &name, const QString &groupsJson, bool atSignIn);
    Q_SCRIPTABLE bool OpenLayout(const QString &id);
    Q_SCRIPTABLE bool RemoveLayout(const QString &id);
    Q_SCRIPTABLE bool SetLayoutAtSignIn(const QString &id, bool on);

Q_SIGNALS:
    Q_SCRIPTABLE void FactChanged(const QVariantMap &fact);
    // The person asked to see the notifications that came in during Do Not
    // Disturb; whatever keeps them opens them.
    Q_SCRIPTABLE void OpenNotifications();
    // A layout was saved, removed or switched.
    Q_SCRIPTABLE void LayoutsChanged();
    // The upkeep cards Actions lists changed, as when the app store came or
    // went: a surface asks Actions again.
    Q_SCRIPTABLE void ActionsChanged();
    // A layout's run is over, with the names of the apps that didn't come.
    Q_SCRIPTABLE void LayoutOpened(const QString &id, int placed, int missed, const QStringList &missedNames);

private:
    Core *m_core;
};

} // namespace splitrock::core
