// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QList>
#include <QObject>
#include <QVariantMap>

namespace splitrock::core {

class Core;

// Core's facts, offered on the session bus to the desktop's surfaces
// (docs/CORE.md § On the bus). A surface that finds nothing here carries on as
// it always does.
class CoreService : public QObject
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

Q_SIGNALS:
    Q_SCRIPTABLE void FactChanged(const QVariantMap &fact);
    // The person asked to see the notifications that came in during Do Not
    // Disturb; whatever keeps them opens them.
    Q_SCRIPTABLE void OpenNotifications();

private:
    Core *m_core;
};

} // namespace splitrock::core
