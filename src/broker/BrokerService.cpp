// SPDX-License-Identifier: GPL-2.0-or-later
#include "broker/BrokerService.h"

#include "broker/Broker.h"

#include <QDBusArgument>
#include <QDBusMetaType>

namespace splitrock::broker {

namespace {

// A dictionary or list from the bus, as plain Qt values all the way down:
// nested messages arrive wrapped as QDBusArgument.
QVariant plain(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>()) {
        const QDBusArgument argument = value.value<QDBusArgument>();
        if (argument.currentType() == QDBusArgument::MapType) {
            QVariantMap map;
            argument >> map;
            return plain(map);
        }
        if (argument.currentType() == QDBusArgument::ArrayType) {
            QVariantList list;
            argument.beginArray();
            while (!argument.atEnd()) {
                QVariant item;
                argument >> item;
                list.append(plain(item));
            }
            argument.endArray();
            return list;
        }
        return {};
    }
    if (value.typeId() == QMetaType::QVariantMap) {
        QVariantMap map = value.toMap();
        for (auto it = map.begin(); it != map.end(); ++it)
            it.value() = plain(it.value());
        return map;
    }
    if (value.typeId() == QMetaType::QVariantList) {
        QVariantList list = value.toList();
        for (QVariant &item : list)
            item = plain(item);
        return list;
    }
    return value;
}

} // namespace

BrokerService::BrokerService(Broker *broker, QObject *parent)
    : QObject(parent)
    , m_broker(broker)
{
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

} // namespace splitrock::broker
