// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace splitrock::broker {

class Broker;
class Toolbox;

// The broker on the session bus (docs/BROKER.md § On the bus). The bus starts
// the program on the first call; it ends on its own when idle. Version 0: for
// testing, and may still change. The apps' tools (Tools, Call and Run) were
// added within it, since Mail starts only a broker of the version it knows.
class BrokerService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.SplitRock.Broker")

public:
    static constexpr uint Version = 0;
    static constexpr char Service[] = "io.github.carlsonjm.splitrock.Broker";
    static constexpr char Path[] = "/Broker";

    // toolbox: the apps' tools, or none where they aren't offered.
    explicit BrokerService(Broker *broker, Toolbox *toolbox = nullptr, QObject *parent = nullptr);

    bool publish(QDBusConnection bus);

    // A dictionary or list from the bus as plain Qt values all the way down.
    static QVariant plain(const QVariant &value);

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
    // The apps' tools (docs/BROKER.md § Tools): each as its app's file
    // describes it. Call answers a read with what the app gave and a write
    // with a card; Run makes the write a card stands for, after the tap.
    Q_SCRIPTABLE QVariantList Tools();
    Q_SCRIPTABLE QVariantMap Call(const QString &tool, const QVariantMap &args, const QString &caller);
    Q_SCRIPTABLE QVariantMap Run(const QString &cardToken);

Q_SIGNALS:
    Q_SCRIPTABLE void Sorted(uint id, const QVariantMap &result);
    Q_SCRIPTABLE void Said(uint id, const QString &text);
    Q_SCRIPTABLE void Drafted(uint id, const QVariantMap &result);
    Q_SCRIPTABLE void Answered(uint id, const QVariantMap &result);
    Q_SCRIPTABLE void Failed(uint id, const QString &problem);
    Q_SCRIPTABLE void ModelChanged(const QVariantMap &model);

private:
    // The reply to the call being answered, sent once the app has answered.
    std::function<void(const QVariantMap &)> answerLater();

    Broker *m_broker;
    Toolbox *m_toolbox;
};

} // namespace splitrock::broker
