// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace splitrock::broker {

class Broker;

// The broker on the session bus (docs/BROKER.md § On the bus). The bus starts
// the program on the first call; it ends on its own when idle. Version 0: for
// testing, and may still change.
class BrokerService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.SplitRock.Broker")

public:
    static constexpr uint Version = 0;
    static constexpr char Service[] = "io.github.carlsonjm.splitrock.Broker";
    static constexpr char Path[] = "/Broker";

    explicit BrokerService(Broker *broker, QObject *parent = nullptr);

    bool publish(QDBusConnection bus);

public Q_SLOTS:
    Q_SCRIPTABLE uint ProtocolVersion() const;
    Q_SCRIPTABLE QVariantMap Model() const;
    Q_SCRIPTABLE QVariantMap Codex() const;
    Q_SCRIPTABLE void Warm();
    Q_SCRIPTABLE uint SortMail(const QVariantMap &message);
    Q_SCRIPTABLE uint DraftReply(const QVariantMap &conversation, const QString &styleNote, const QString &ask);
    Q_SCRIPTABLE uint Clerk(const QString &question, const QVariantList &found);
    Q_SCRIPTABLE uint StyleNote(const QVariantList &sent);
    Q_SCRIPTABLE void Cancel(uint id);

Q_SIGNALS:
    Q_SCRIPTABLE void Sorted(uint id, const QVariantMap &result);
    Q_SCRIPTABLE void Said(uint id, const QString &text);
    Q_SCRIPTABLE void Drafted(uint id, const QVariantMap &result);
    Q_SCRIPTABLE void Answered(uint id, const QVariantMap &result);
    Q_SCRIPTABLE void Failed(uint id, const QString &problem);
    Q_SCRIPTABLE void ModelChanged(const QVariantMap &model);

private:
    Broker *m_broker;
};

} // namespace splitrock::broker
