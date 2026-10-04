// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// Split Rock's own Agent Client Protocol layer, pinned to one protocol version.
// Only what Split Rock uses is modelled; everything else an agent sends is
// tolerated and ignored.
namespace acp {

// The one protocol version Split Rock speaks. An agent that answers with any
// other version is disconnected.
inline constexpr int ProtocolVersion = 1;

// JSON-RPC error codes the protocol gives meaning to.
inline constexpr int AuthRequiredCode = -32000;
inline constexpr int MethodNotFoundCode = -32601;
inline constexpr int InvalidParamsCode = -32602;
// Not a protocol code: used locally when the agent's program stops while a
// request is still waiting for its answer.
inline constexpr int AgentStoppedCode = -32099;

struct RpcError {
    int code = 0;
    QString message;
    QJsonValue data;

    // The reason an agent attached to an error, when it gave one.
    QString reason() const;
};

// One way of signing in that an agent advertises at initialize.
struct AuthMethod {
    enum class Kind {
        // The agent signs the person in itself, through `authenticate`.
        Agent,
        // The client runs the agent's program again with extra arguments, for
        // the person to sign in through what that program shows.
        Terminal,
        // Any other kind, such as asking the client to supply a key.
        Other,
    };

    QString id;
    QString name;
    QString description;
    Kind kind = Kind::Agent;
    QString typeName;
    QStringList terminalArgs;
    QMap<QString, QString> terminalEnv;
    QJsonObject meta;
};

QList<AuthMethod> parseAuthMethods(const QJsonArray &methods);

struct PermissionOption {
    QString id;
    QString name;
    QString kind;
};

QList<PermissionOption> parsePermissionOptions(const QJsonArray &options);

// The option that refuses, preferring "only this time" so that the agent may
// ask again later. Empty when the agent offered no way to refuse.
QString refusalOptionId(const QList<PermissionOption> &options);
// The option that allows, only this time. Empty when the agent offered none.
QString allowOnceOptionId(const QList<PermissionOption> &options);

// What Split Rock keeps of a tool call: enough to describe it in plain words.
struct ToolCall {
    QString id;
    QString title;
    QString kind;
    QString status;
    QJsonValue rawInput;
    QStringList paths;
    QStringList diffPaths;

    // The command line, when the tool runs one.
    QString command() const;
    // True for the kinds of tool that change something.
    bool changesSomething() const;
};

// Applies a `tool_call` or `tool_call_update` object to what is known of it.
void mergeToolCall(ToolCall &call, const QJsonObject &update);

// The text of a content block, or empty for any other kind of content.
QString contentText(const QJsonValue &block);

} // namespace acp
