// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Connection.h"

#include <QObject>

namespace acp {

// The client side of one agent connection: one program, at most one session.
//
// The client never lets the agent change anything. A request for permission is
// refused the moment it arrives, before anything else hears of it; there is no
// way to answer one with yes. File and terminal access are not offered.
class Client : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(const std::optional<RpcError> &error)>;

    struct AgentInfo {
        QString name;
        QString title;
        QString version;
    };

    struct SessionMode {
        QString id;
        QString name;
    };

    explicit Client(QObject *parent = nullptr);

    void start(const LaunchSpec &spec);
    void stop();
    void kill() { m_connection->kill(); }
    Connection *connection() const { return m_connection; }

    // Negotiates the protocol version. Fails when the agent answers with any
    // version other than the one Split Rock speaks.
    void initialize(const QString &clientVersion, Done done);
    void authenticate(const QString &methodId, Done done);
    void newSession(const QString &workingDirectory, Done done);
    void setMode(const QString &modeId, Done done);
    void prompt(const QJsonArray &blocks, std::function<void(const QString &stopReason, const std::optional<RpcError> &error)> done);
    void cancel();

    AgentInfo agentInfo() const { return m_agentInfo; }
    QList<AuthMethod> authMethods() const { return m_authMethods; }
    QString sessionId() const { return m_sessionId; }
    QList<SessionMode> modes() const { return m_modes; }
    QString currentMode() const { return m_currentMode; }

    static QJsonObject clientCapabilities();

Q_SIGNALS:
    void messageChunk(const QString &text);
    void thoughtChunk(const QString &text);
    void toolCallChanged(const acp::ToolCall &call);
    // A change the agent asked to make, already refused.
    void changeRefused(const acp::ToolCall &call, const QString &refusalOptionId);
    void stopped(const QString &reason, bool expected);
    void diagnostic(const QString &line);

private:
    void handleRequest(const QJsonValue &id, const QString &method, const QJsonObject &params);
    void handleNotification(const QString &method, const QJsonObject &params);

    Connection *m_connection;
    AgentInfo m_agentInfo;
    QList<AuthMethod> m_authMethods;
    QString m_sessionId;
    QList<SessionMode> m_modes;
    QString m_currentMode;
    QHash<QString, ToolCall> m_toolCalls;
};

} // namespace acp
