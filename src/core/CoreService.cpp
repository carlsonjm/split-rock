// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/CoreService.h"

#include "core/Core.h"

#include <QDBusMessage>
#include <QDBusMetaType>

namespace splitrock::core {

CoreService::CoreService(Core *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
    qDBusRegisterMetaType<QList<QVariantMap>>();
    connect(m_core, &Core::factChanged, this, &CoreService::FactChanged);
    connect(m_core, &Core::layoutsChanged, this, &CoreService::LayoutsChanged);
    connect(m_core, &Core::actionsChanged, this, &CoreService::ActionsChanged);
    connect(m_core, &Core::layoutOpened, this, &CoreService::LayoutOpened);
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
    return m_core->offeredActions();
}

bool CoreService::OpenAction(const QString &id, const QString &query)
{
    return m_core->openAction(id, query);
}

void CoreService::FocusEnded(uint notifications)
{
    m_core->focusEnded(int(qMin(notifications, 100000u)));
}

void CoreService::FocusEndedInView(uint notifications)
{
    m_core->focusEnded(int(qMin(notifications, 100000u)), true);
}

void CoreService::NotificationsSeen()
{
    m_core->notificationsSeen();
}

bool CoreService::RemoveApp(const QString &desktopId)
{
    return m_core->removeApp(desktopId);
}

QList<QVariantMap> CoreService::Layouts() const
{
    return m_core->layoutsForBus();
}

QVariantMap CoreService::CurrentLayout()
{
    if (!calledFromDBus()) {
        QVariantMap answer;
        m_core->currentLayout([&answer](const QVariantMap &desk) { answer = desk; });
        return answer;
    }
    // Cards answers in its own time; the caller's reply waits for it.
    setDelayedReply(true);
    const QDBusMessage call = message();
    QDBusConnection bus = connection();
    m_core->currentLayout([call, bus](const QVariantMap &desk) mutable { bus.send(call.createReply(desk)); });
    return {};
}

QString CoreService::SaveLayout(const QString &name, const QString &groupsJson, bool atSignIn)
{
    return m_core->saveLayout(name, groupsJson, atSignIn);
}

bool CoreService::OpenLayout(const QString &id)
{
    if (!calledFromDBus())
        return m_core->openLayout(id);
    const QDBusMessage call = message();
    QDBusConnection bus = connection();
    const bool asked = m_core->openLayout(id, [call, bus](bool accepted) mutable { bus.send(call.createReply(accepted)); });
    // Refused at once: the plain reply says so. Asked: the reply waits for
    // Cards.
    if (asked)
        setDelayedReply(true);
    return false;
}

bool CoreService::RemoveLayout(const QString &id)
{
    return m_core->removeLayout(id);
}

bool CoreService::SetLayoutAtSignIn(const QString &id, bool on)
{
    return m_core->setLayoutAtSignIn(id, on);
}

} // namespace splitrock::core
