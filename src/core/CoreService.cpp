// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/CoreService.h"

#include "core/Core.h"

#include <QDBusMetaType>

namespace splitrock::core {

CoreService::CoreService(Core *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
    qDBusRegisterMetaType<QList<QVariantMap>>();
    connect(m_core, &Core::factChanged, this, &CoreService::FactChanged);
}

bool CoreService::publish(QDBusConnection bus)
{
    return bus.registerObject(QLatin1String(Path), this, QDBusConnection::ExportScriptableContents);
}

uint CoreService::ProtocolVersion() const
{
    return Version;
}

QList<QVariantMap> CoreService::Facts() const
{
    return m_core->facts();
}

bool CoreService::Act(const QString &topic, const QString &action)
{
    return m_core->act(topic, action);
}

QList<QVariantMap> CoreService::Actions() const
{
    return Core::actions();
}

bool CoreService::OpenAction(const QString &id, const QString &query)
{
    return m_core->openAction(id, query);
}

void CoreService::FocusEnded(uint notifications)
{
    m_core->focusEnded(int(qMin(notifications, 100000u)));
}

bool CoreService::RemoveApp(const QString &desktopId)
{
    return m_core->removeApp(desktopId);
}

} // namespace splitrock::core
