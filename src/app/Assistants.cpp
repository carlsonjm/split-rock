// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Assistants.h"

#include "app/Fence.h"
#include "app/Product.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>

namespace splitrock {

namespace {

QString keyReason()
{
    return QStringLiteral("It needs a key that %1 would have to hold, and %1 never holds one.").arg(productName());
}

// The major version of the Node.js on this computer, or 0 when there is none.
int nodeMajorVersion(const QString &node)
{
    static QHash<QString, int> cache;
    if (cache.contains(node))
        return cache.value(node);
    QProcess process;
    process.start(node, {QStringLiteral("--version")});
    int major = 0;
    if (process.waitForFinished(5000)) {
        const QString text = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        static const QRegularExpression version(QStringLiteral("^v(\\d+)\\."));
        const QRegularExpressionMatch match = version.match(text);
        if (match.hasMatch())
            major = match.captured(1).toInt();
    }
    cache.insert(node, major);
    return major;
}

Assistant nodeAssistant(const QString &id, const QString &name, const QString &adapterDir,
                        const QString &entry, int nodeMajor, const QString &stateDir)
{
    Assistant a;
    a.id = id;
    a.name = name;
    a.home = QDir(stateDir).filePath(id);
    const QString node = QStandardPaths::findExecutable(QStringLiteral("node"));
    const QString script = QDir(adapterDir).filePath(entry);
    a.program = node;
    a.arguments = {script};
    a.readable = {QDir(adapterDir).absolutePath()};
    if (fenceProgram().isEmpty()) {
        a.unavailable = QStringLiteral("%1 can't be kept to the notebook on this computer, which lacks the "
                                       "sandbox it runs in (bubblewrap).").arg(productName());
    } else if (node.isEmpty()) {
        a.unavailable = QStringLiteral("%1 runs on Node.js, which this computer does not have.").arg(name);
    } else if (!QFileInfo::exists(script)) {
        a.unavailable = QStringLiteral("%1 has not been fetched onto this computer yet.").arg(name);
    } else if (nodeMajorVersion(node) < nodeMajor) {
        a.unavailable = QStringLiteral("%1 needs Node.js %2 or later.").arg(name).arg(nodeMajor);
    }
    return a;
}

} // namespace

QList<SignInChoice> classifySignIn(const Assistant &assistant, const QList<acp::AuthMethod> &methods)
{
    QList<SignInChoice> choices;
    for (const acp::AuthMethod &method : methods) {
        SignInChoice choice;
        choice.method = method;
        if (assistant.hiddenMethods.contains(method.id)) {
            choice.whyNot = assistant.hiddenMethods.value(method.id);
        } else if (method.meta.contains(QStringLiteral("api-key")) || method.meta.contains(QStringLiteral("gateway"))) {
            choice.whyNot = keyReason();
        } else if (method.kind == acp::AuthMethod::Kind::Agent) {
            choice.how = SignInChoice::How::Agent;
        } else if (method.kind == acp::AuthMethod::Kind::Terminal) {
            choice.how = SignInChoice::How::WithoutTerminal;
        } else {
            choice.whyNot = keyReason();
        }
        choices.append(choice);
    }
    return choices;
}

QList<Assistant> realAssistants(const QString &adaptersDir, const QString &stateDir)
{
    const QDir adapters(adaptersDir);
    QList<Assistant> list;

    // Codex alone: the one paid assistant tested to ship with Genie on this
    // computer (DECISIONS.md, Two assistants: on this computer, and Codex).
    Assistant codex = nodeAssistant(QStringLiteral("codex"), QStringLiteral("Codex"),
                                    adapters.filePath(QStringLiteral("codex")),
                                    QStringLiteral("node_modules/@zed-industries/codex-acp/bin/codex-acp.js"), 18, stateDir);
    codex.cost = QStringLiteral("Needs a paid ChatGPT plan. You sign in to ChatGPT in your browser.");
    codex.environment.insert(QStringLiteral("CODEX_HOME"), codex.home);
    codex.hiddenMethods = {{QStringLiteral("codex-api-key"), keyReason()}, {QStringLiteral("openai-api-key"), keyReason()}};
    codex.askFirstMode = QStringLiteral("read-only");
    codex.conversationLeftovers = {QStringLiteral("sessions"), QStringLiteral("archived_sessions"),
                                   QStringLiteral("history.jsonl"), QStringLiteral("log"),
                                   QStringLiteral("state_*.sqlite*"), QStringLiteral("logs_*.sqlite*"),
                                   QStringLiteral("memories_*.sqlite*"), QStringLiteral("goals_*.sqlite*")};
    list.append(codex);

    return list;
}

Assistant localAssistant(const QString &program, const QString &engine, const QString &stateDir)
{
    Assistant a;
    a.id = QStringLiteral("local");
    a.name = QStringLiteral("%1 on this computer").arg(productName());
    a.cost = QStringLiteral("Free, with no account. It runs on this computer, so nothing you ask leaves it. "
                            "It needs a one-time download first.");
    a.local = true;
    a.program = program;
    a.home = QDir(stateDir).filePath(a.id);
    a.readable = {QFileInfo(program).absolutePath(), QFileInfo(engine).absolutePath()};
    // The graphics chip, for the model to run on.
    a.devices = {QStringLiteral("/dev/dri")};
    if (fenceProgram().isEmpty())
        a.unavailable = QStringLiteral("%1 can't be kept to the notebook on this computer, which lacks the "
                                       "sandbox it runs in (bubblewrap).").arg(productName());
    else if (!QFileInfo(program).isExecutable())
        a.unavailable = QStringLiteral("%1 on this computer isn't installed.").arg(productName());
    else if (engine.isEmpty())
        a.unavailable = QStringLiteral("The engine %1 on this computer runs on, llama.cpp, isn't installed.").arg(productName());
    return a;
}

QString findEngine()
{
    // Shuffle's own build of llama.cpp first, then one the person installed.
    const QString own = QStringLiteral(SPLIT_ROCK_ENGINE_DIR "/llama-server");
    if (QFileInfo(own).isExecutable())
        return own;
    return QStandardPaths::findExecutable(QStringLiteral("llama-server"));
}

Assistant pretendAssistant(const QString &program, const QString &script, const QString &stateDir)
{
    Assistant a;
    a.id = QStringLiteral("pretend");
    a.name = QStringLiteral("Pretend assistant");
    a.cost = QStringLiteral("Free: it follows a script and reaches nothing outside this program.");
    a.program = program;
    a.arguments = {script};
    a.home = QDir(stateDir).filePath(a.id);
    a.environment.insert(QStringLiteral("SPLIT_ROCK_PRETEND_HOME"), a.home);
    a.hiddenMethods = {{QStringLiteral("pretend-key"), keyReason()}};
    a.readable = {QFileInfo(program).absolutePath(), QFileInfo(script).absoluteFilePath()};
    a.mayRunUnfenced = true;
    a.askFirstMode = QStringLiteral("ask-first");
    a.conversationLeftovers = {QStringLiteral("transcripts")};
    if (!QFileInfo::exists(program))
        a.unavailable = QStringLiteral("The pretend assistant was not built.");
    else if (!QFileInfo::exists(script))
        a.unavailable = QStringLiteral("The pretend assistant's script was not found.");
    return a;
}

QProcessEnvironment cleanEnvironment(const QProcessEnvironment &base)
{
    static const QRegularExpression dropped(QStringLiteral(
        "^(CLAUDE.*|ANTHROPIC_.*|OPENAI_.*|CODEX_.*|GEMINI_.*|GOOGLE_API_KEY|GOOGLE_APPLICATION_CREDENTIALS"
        "|GOOGLE_CLOUD_.*|GOOGLE_GENAI_.*|AWS_.*|NO_BROWSER|SPLIT_ROCK_.*)$"));
    QProcessEnvironment env;
    for (const QString &key : base.keys()) {
        if (!dropped.match(key).hasMatch())
            env.insert(key, base.value(key));
    }
    return env;
}

static QProcessEnvironment assistantEnvironment(const Assistant &assistant, const QProcessEnvironment &base)
{
    QProcessEnvironment env = cleanEnvironment(base);
    for (auto it = assistant.environment.begin(); it != assistant.environment.end(); ++it)
        env.insert(it.key(), it.value());
    return env;
}

acp::LaunchSpec serveLaunch(const Assistant &assistant, const QProcessEnvironment &base, const QString &workingDirectory)
{
    return {assistant.program, assistant.arguments + assistant.serveArguments,
            assistantEnvironment(assistant, base), workingDirectory};
}

acp::LaunchSpec signInLaunch(const Assistant &assistant, const acp::AuthMethod &method,
                             const QProcessEnvironment &base, const QString &workingDirectory)
{
    QProcessEnvironment env = assistantEnvironment(assistant, base);
    for (auto it = method.terminalEnv.begin(); it != method.terminalEnv.end(); ++it) {
        // The method may not undo the assistant's separation from the person's
        // own copy of it.
        if (!assistant.environment.contains(it.key()))
            env.insert(it.key(), it.value());
    }
    return {assistant.program, assistant.arguments + method.terminalArgs, env, workingDirectory};
}

void removeConversationLeftovers(const Assistant &assistant)
{
    if (assistant.home.isEmpty())
        return;
    const QDir home(assistant.home);
    if (!home.exists())
        return;
    for (const QString &leftover : assistant.conversationLeftovers) {
        if (leftover.contains(QLatin1String("..")) || QDir::isAbsolutePath(leftover))
            continue;
        const qsizetype slash = leftover.lastIndexOf(QLatin1Char('/'));
        const QDir dir(slash < 0 ? home.path() : home.filePath(leftover.left(slash)));
        const QString pattern = slash < 0 ? leftover : leftover.mid(slash + 1);
        if (!dir.exists())
            continue;
        const QFileInfoList entries = dir.entryInfoList({pattern}, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            // Never follow a link out of the assistant's folder.
            if (entry.isSymLink() || !entry.absoluteFilePath().startsWith(home.absolutePath() + QLatin1Char('/')))
                continue;
            if (entry.isDir())
                QDir(entry.absoluteFilePath()).removeRecursively();
            else
                QFile::remove(entry.absoluteFilePath());
        }
    }
}

} // namespace splitrock
