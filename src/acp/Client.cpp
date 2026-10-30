// SPDX-License-Identifier: GPL-2.0-or-later
#include "acp/Client.h"

namespace acp {

Client::Client(QObject *parent)
    : QObject(parent)
    , m_connection(new Connection(this))
{
    connect(m_connection, &Connection::requestReceived, this, &Client::handleRequest);
    connect(m_connection, &Connection::notificationReceived, this, &Client::handleNotification);
    connect(m_connection, &Connection::stopped, this, &Client::stopped);
    connect(m_connection, &Connection::diagnostic, this, &Client::diagnostic);
}

void Client::start(const LaunchSpec &spec)
{
    m_connection->start(spec);
}

void Client::stop()
{
    m_connection->stop();
}

QJsonObject Client::clientCapabilities()
{
    // No file or terminal access: the agent gets nothing from Split Rock that
    // could change the computer. Terminal sign-in is declared so that agents
    // show every sign-in they have; Split Rock decides which it offers.
    return {{QStringLiteral("fs"), QJsonObject{{QStringLiteral("readTextFile"), false},
                                               {QStringLiteral("writeTextFile"), false}}},
            {QStringLiteral("terminal"), false},
            {QStringLiteral("auth"), QJsonObject{{QStringLiteral("terminal"), true}}}};
}

void Client::initialize(const QString &clientVersion, const QString &clientTitle, Done done)
{
    const QJsonObject params{
        {QStringLiteral("protocolVersion"), ProtocolVersion},
        {QStringLiteral("clientCapabilities"), clientCapabilities()},
        {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("split-rock")},
                                                   {QStringLiteral("title"), clientTitle},
                                                   {QStringLiteral("version"), clientVersion}}},
    };
    m_connection->request(QStringLiteral("initialize"), params,
                          [this, done](const QJsonValue &result, const std::optional<RpcError> &error) {
        if (error) {
            done(error);
            return;
        }
        const QJsonObject object = result.toObject();
        const int version = object.value(QStringLiteral("protocolVersion")).toInt(-1);
        if (version != ProtocolVersion) {
            done(RpcError{InvalidParamsCode,
                          QStringLiteral("The assistant speaks protocol version %1; Split Rock speaks version %2.")
                              .arg(version).arg(ProtocolVersion),
                          {}});
            return;
        }
        const QJsonObject info = object.value(QStringLiteral("agentInfo")).toObject();
        m_agentInfo = {info.value(QStringLiteral("name")).toString(),
                       info.value(QStringLiteral("title")).toString(),
                       info.value(QStringLiteral("version")).toString()};
        m_authMethods = parseAuthMethods(object.value(QStringLiteral("authMethods")).toArray());
        done(std::nullopt);
    });
}

void Client::authenticate(const QString &methodId, Done done)
{
    m_connection->request(QStringLiteral("authenticate"), {{QStringLiteral("methodId"), methodId}},
                          [done](const QJsonValue &, const std::optional<RpcError> &error) { done(error); });
}

void Client::newSession(const QString &workingDirectory, Done done)
{
    const QJsonObject params{{QStringLiteral("cwd"), workingDirectory},
                             {QStringLiteral("mcpServers"), m_mcpServers}};
    m_connection->request(QStringLiteral("session/new"), params,
                          [this, done](const QJsonValue &result, const std::optional<RpcError> &error) {
        if (error) {
            done(error);
            return;
        }
        const QJsonObject object = result.toObject();
        m_sessionId = object.value(QStringLiteral("sessionId")).toString();
        m_toolCalls.clear();
        m_modes.clear();
        const QJsonObject modes = object.value(QStringLiteral("modes")).toObject();
        m_currentMode = modes.value(QStringLiteral("currentModeId")).toString();
        for (const QJsonValue &mode : modes.value(QStringLiteral("availableModes")).toArray()) {
            const QJsonObject m = mode.toObject();
            m_modes.append({m.value(QStringLiteral("id")).toString(), m.value(QStringLiteral("name")).toString()});
        }
        if (m_sessionId.isEmpty()) {
            done(RpcError{InvalidParamsCode, QStringLiteral("The assistant started no session."), {}});
            return;
        }
        done(std::nullopt);
    });
}

void Client::setMode(const QString &modeId, Done done)
{
    const QJsonObject params{{QStringLiteral("sessionId"), m_sessionId}, {QStringLiteral("modeId"), modeId}};
    m_connection->request(QStringLiteral("session/set_mode"), params,
                          [this, modeId, done](const QJsonValue &, const std::optional<RpcError> &error) {
        if (!error)
            m_currentMode = modeId;
        done(error);
    });
}

void Client::prompt(const QJsonArray &blocks,
                    std::function<void(const QString &, const std::optional<RpcError> &)> done)
{
    const QJsonObject params{{QStringLiteral("sessionId"), m_sessionId}, {QStringLiteral("prompt"), blocks}};
    m_connection->request(QStringLiteral("session/prompt"), params,
                          [this, done](const QJsonValue &result, const std::optional<RpcError> &error) {
        m_lastPromptMeta = result.toObject().value(QStringLiteral("_meta")).toObject();
        done(result.toObject().value(QStringLiteral("stopReason")).toString(), error);
    });
}

void Client::cancel()
{
    if (!m_sessionId.isEmpty())
        m_connection->notify(QStringLiteral("session/cancel"), {{QStringLiteral("sessionId"), m_sessionId}});
}

void Client::handleRequest(const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    if (method == QLatin1String("session/request_permission")) {
        const QJsonObject update = params.value(QStringLiteral("toolCall")).toObject();
        const QString callId = update.value(QStringLiteral("toolCallId")).toString();
        ToolCall call = m_toolCalls.value(callId);
        mergeToolCall(call, update);
        m_toolCalls.insert(call.id, call);

        const QList<PermissionOption> options = parsePermissionOptions(params.value(QStringLiteral("options")).toArray());
        const QString allowOnce = allowOnceOptionId(options);
        if (m_allow && !allowOnce.isEmpty() && m_allow(call)) {
            m_connection->respond(id, QJsonObject{{QStringLiteral("outcome"),
                                                   QJsonObject{{QStringLiteral("outcome"), QStringLiteral("selected")},
                                                               {QStringLiteral("optionId"), allowOnce}}}});
            Q_EMIT requestAllowed(call);
            return;
        }
        const QString refusal = refusalOptionId(options);
        // Answered first, so that nothing the person does, or fails to do,
        // can delay or change the answer.
        QJsonObject outcome;
        if (refusal.isEmpty()) {
            // No option refuses, so the request is withdrawn instead: the agent
            // must treat it as not allowed.
            outcome = {{QStringLiteral("outcome"), QStringLiteral("cancelled")}};
        } else {
            outcome = {{QStringLiteral("outcome"), QStringLiteral("selected")},
                       {QStringLiteral("optionId"), refusal}};
        }
        m_connection->respond(id, QJsonObject{{QStringLiteral("outcome"), outcome}});
        Q_EMIT changeRefused(call, refusal);
        return;
    }
    // File and terminal methods were not offered at initialize; nothing else
    // is answered either.
    m_connection->respondError(id, MethodNotFoundCode, QStringLiteral("Split Rock does not offer %1.").arg(method));
}

void Client::handleNotification(const QString &method, const QJsonObject &params)
{
    if (method != QLatin1String("session/update"))
        return;
    if (params.value(QStringLiteral("sessionId")).toString() != m_sessionId)
        return;
    const QJsonObject update = params.value(QStringLiteral("update")).toObject();
    const QString kind = update.value(QStringLiteral("sessionUpdate")).toString();
    if (kind == QLatin1String("agent_message_chunk")) {
        const QString text = contentText(update.value(QStringLiteral("content")));
        if (!text.isEmpty())
            Q_EMIT messageChunk(text);
    } else if (kind == QLatin1String("agent_thought_chunk")) {
        Q_EMIT thoughtChunk(contentText(update.value(QStringLiteral("content"))));
    } else if (kind == QLatin1String("tool_call") || kind == QLatin1String("tool_call_update")) {
        const QString callId = update.value(QStringLiteral("toolCallId")).toString();
        ToolCall call = m_toolCalls.value(callId);
        mergeToolCall(call, update);
        m_toolCalls.insert(call.id, call);
        Q_EMIT toolCallChanged(call);
    } else if (kind == QLatin1String("current_mode_update")) {
        m_currentMode = update.value(QStringLiteral("currentModeId")).toString();
    }
}

} // namespace acp
