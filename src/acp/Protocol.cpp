// SPDX-License-Identifier: GPL-2.0-or-later
#include "acp/Protocol.h"

namespace acp {

QString RpcError::reason() const
{
    return data.toObject().value(QStringLiteral("reason")).toString();
}

QList<AuthMethod> parseAuthMethods(const QJsonArray &methods)
{
    QList<AuthMethod> result;
    for (const QJsonValue &value : methods) {
        const QJsonObject object = value.toObject();
        AuthMethod method;
        method.id = object.value(QStringLiteral("id")).toString();
        method.name = object.value(QStringLiteral("name")).toString();
        method.description = object.value(QStringLiteral("description")).toString();
        method.meta = object.value(QStringLiteral("_meta")).toObject();
        // The protocol skips a method it cannot read rather than failing.
        if (method.id.isEmpty() || method.name.isEmpty())
            continue;
        // No type means the agent signs in itself.
        method.typeName = object.value(QStringLiteral("type")).toString(QStringLiteral("agent"));
        if (method.typeName == QLatin1String("agent")) {
            method.kind = AuthMethod::Kind::Agent;
        } else if (method.typeName == QLatin1String("terminal")) {
            method.kind = AuthMethod::Kind::Terminal;
            for (const QJsonValue &arg : object.value(QStringLiteral("args")).toArray()) {
                if (arg.isString())
                    method.terminalArgs.append(arg.toString());
            }
            const QJsonObject env = object.value(QStringLiteral("env")).toObject();
            for (auto it = env.begin(); it != env.end(); ++it) {
                if (it.value().isString())
                    method.terminalEnv.insert(it.key(), it.value().toString());
            }
        } else {
            method.kind = AuthMethod::Kind::Other;
        }
        result.append(method);
    }
    return result;
}

QList<PermissionOption> parsePermissionOptions(const QJsonArray &options)
{
    QList<PermissionOption> result;
    for (const QJsonValue &value : options) {
        const QJsonObject object = value.toObject();
        result.append({object.value(QStringLiteral("optionId")).toString(),
                       object.value(QStringLiteral("name")).toString(),
                       object.value(QStringLiteral("kind")).toString()});
    }
    return result;
}

QString refusalOptionId(const QList<PermissionOption> &options)
{
    for (const QString &kind : {QStringLiteral("reject_once"), QStringLiteral("reject_always")}) {
        for (const PermissionOption &option : options) {
            if (option.kind == kind && !option.id.isEmpty())
                return option.id;
        }
    }
    return {};
}

QString allowOnceOptionId(const QList<PermissionOption> &options)
{
    for (const PermissionOption &option : options) {
        if (option.kind == QLatin1String("allow_once") && !option.id.isEmpty())
            return option.id;
    }
    return {};
}

QString ToolCall::command() const
{
    const QJsonObject input = rawInput.toObject();
    const QJsonValue command = input.value(QStringLiteral("command"));
    if (command.isString())
        return command.toString();
    if (command.isArray()) {
        QStringList parts;
        for (const QJsonValue &part : command.toArray())
            parts.append(part.toString());
        return parts.join(QLatin1Char(' '));
    }
    return {};
}

bool ToolCall::changesSomething() const
{
    return kind == QLatin1String("edit") || kind == QLatin1String("delete")
        || kind == QLatin1String("move") || kind == QLatin1String("execute");
}

void mergeToolCall(ToolCall &call, const QJsonObject &update)
{
    const auto text = [&update](const char *key) {
        return update.value(QLatin1String(key)).toString();
    };
    if (call.id.isEmpty())
        call.id = text("toolCallId");
    if (!text("title").isEmpty())
        call.title = text("title");
    if (!text("kind").isEmpty())
        call.kind = text("kind");
    if (!text("status").isEmpty())
        call.status = text("status");
    if (update.contains(QStringLiteral("rawInput")) && !update.value(QStringLiteral("rawInput")).isNull())
        call.rawInput = update.value(QStringLiteral("rawInput"));
    if (update.contains(QStringLiteral("locations"))) {
        call.paths.clear();
        for (const QJsonValue &location : update.value(QStringLiteral("locations")).toArray()) {
            const QString path = location.toObject().value(QStringLiteral("path")).toString();
            if (!path.isEmpty() && !call.paths.contains(path))
                call.paths.append(path);
        }
    }
    if (update.contains(QStringLiteral("content"))) {
        call.diffPaths.clear();
        for (const QJsonValue &item : update.value(QStringLiteral("content")).toArray()) {
            const QJsonObject object = item.toObject();
            if (object.value(QStringLiteral("type")).toString() == QLatin1String("diff")) {
                const QString path = object.value(QStringLiteral("path")).toString();
                if (!path.isEmpty())
                    call.diffPaths.append(path);
            }
        }
    }
}

QString contentText(const QJsonValue &block)
{
    const QJsonObject object = block.toObject();
    if (object.value(QStringLiteral("type")).toString() != QLatin1String("text"))
        return {};
    return object.value(QStringLiteral("text")).toString();
}

} // namespace acp
