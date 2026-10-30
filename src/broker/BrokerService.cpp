// SPDX-License-Identifier: GPL-2.0-or-later
#include "broker/BrokerService.h"

#include "broker/Broker.h"
#include "broker/Toolbox.h"
#include "tools/Capabilities.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>

namespace splitrock::broker {

// Nested messages arrive wrapped as QDBusArgument: Mail's conversation holds
// its messages as aa{sv} and each mail its attachments as as.
QVariant BrokerService::plain(const QVariant &value)
{
    return tools::fromBus(value);
}

BrokerService::BrokerService(Broker *broker, Toolbox *toolbox, QObject *parent)
    : QObject(parent)
    , m_broker(broker)
    , m_toolbox(toolbox)
{
    if (!m_broker)
        return;
    connect(m_broker, &Broker::sorted, this, &BrokerService::Sorted);
    connect(m_broker, &Broker::said, this, &BrokerService::Said);
    connect(m_broker, &Broker::drafted, this, &BrokerService::Drafted);
    connect(m_broker, &Broker::answered, this, &BrokerService::Answered);
    connect(m_broker, &Broker::failed, this, &BrokerService::Failed);
    connect(m_broker, &Broker::modelChanged, this, &BrokerService::ModelChanged);
}

bool BrokerService::publish(QDBusConnection bus)
{
    return bus.registerObject(QLatin1String(Path), this, QDBusConnection::ExportScriptableContents);
}

uint BrokerService::ProtocolVersion() const
{
    return Version;
}

QVariantMap BrokerService::Model() const
{
    return m_broker->model();
}

QVariantMap BrokerService::Codex() const
{
    return m_broker->codex();
}

void BrokerService::Warm()
{
    m_broker->warm();
}

uint BrokerService::SortMail(const QVariantMap &message)
{
    return m_broker->sortMail(plain(message).toMap());
}

uint BrokerService::DraftReply(const QVariantMap &conversation, const QString &styleNote, const QString &ask)
{
    return m_broker->draftReply(plain(conversation).toMap(), styleNote, ask);
}

uint BrokerService::Clerk(const QString &question, const QVariantList &found)
{
    return m_broker->clerk(question, plain(found).toList());
}

uint BrokerService::StyleNote(const QVariantList &sent)
{
    return m_broker->styleNote(plain(sent).toList());
}

void BrokerService::Cancel(uint id)
{
    m_broker->cancel(id);
}

QVariantList BrokerService::Tools()
{
    return m_toolbox ? m_toolbox->tools() : QVariantList();
}

QVariantMap BrokerService::Call(const QString &tool, const QVariantMap &args, const QString &caller)
{
    if (!m_toolbox || !calledFromDBus())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), QStringLiteral("No tools are offered here.")}};
    // The app answers in its own time; the reply waits for it.
    setDelayedReply(true);
    m_toolbox->call(tool, plain(args).toMap(), caller, answerLater());
    return {};
}

QVariantMap BrokerService::Run(const QString &cardToken)
{
    if (!m_toolbox || !calledFromDBus())
        return {{QStringLiteral("ok"), false}, {QStringLiteral("problem"), QStringLiteral("No tools are offered here.")}};
    setDelayedReply(true);
    m_toolbox->run(cardToken, answerLater());
    return {};
}

std::function<void(const QVariantMap &)> BrokerService::answerLater()
{
    return [bus = connection(), request = message()](const QVariantMap &result) mutable {
        bus.send(request.createReply(QVariant(result)));
    };
}

} // namespace splitrock::broker
