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

Q_SIGNALS:
    Q_SCRIPTABLE void FactChanged(const QVariantMap &fact);

private:
    Core *m_core;
};

} // namespace splitrock::core
