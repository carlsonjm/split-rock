// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Protocol.h"

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>

#include <functional>
#include <optional>

namespace acp {

// How to start an agent's program.
struct LaunchSpec {
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
    QString workingDirectory;
};

// JSON-RPC 2.0 over the agent program's standard input and output, one message
// per line. The agent's standard error is passed on as diagnostics and never
// shown to the person.
class Connection : public QObject
{
    Q_OBJECT

public:
    using ReplyHandler = std::function<void(const QJsonValue &result, const std::optional<RpcError> &error)>;

    explicit Connection(QObject *parent = nullptr);
    ~Connection() override;

    void start(const LaunchSpec &spec);
    // Ends the program: politely first, then firmly.
    void stop();
    // Ends the program now and forgets every request still waiting, without
    // calling their handlers or reporting the stop.
    void kill();
    bool isRunning() const;

    int request(const QString &method, const QJsonObject &params, ReplyHandler handler);
    void notify(const QString &method, const QJsonObject &params);
    void respond(const QJsonValue &id, const QJsonValue &result);
    void respondError(const QJsonValue &id, int code, const QString &message);

Q_SIGNALS:
    void started();
    void requestReceived(const QJsonValue &id, const QString &method, const QJsonObject &params);
    void notificationReceived(const QString &method, const QJsonObject &params);
    // The program could not start, or stopped. `expected` is true after stop().
    void stopped(const QString &reason, bool expected);
    void diagnostic(const QString &line);

private:
    void send(const QJsonObject &message);
    void readOutput();
    void handleLine(const QByteArray &line);
    void failPending(const QString &message);

    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    QByteArray m_errorBuffer;
    int m_nextId = 1;
    QHash<int, ReplyHandler> m_pending;
    bool m_stopping = false;
    bool m_reportedStop = false;
};

} // namespace acp
