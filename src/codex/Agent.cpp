// SPDX-License-Identifier: GPL-2.0-or-later
#include "codex/Agent.h"

#include "ask/Fence.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace splitrock::codex {

bool Agent::signedIn() const
{
    return !home.isEmpty() && !signInFile.isEmpty() && QFileInfo::exists(QDir(home).filePath(signInFile));
}

bool Agent::signOut() const
{
    removeLeftovers(*this);
    if (home.isEmpty() || signInFile.isEmpty())
        return false;
    const QString file = QDir(home).filePath(signInFile);
    return !QFileInfo::exists(file) || QFile::remove(file);
}

QString findAdapter()
{
    const QString own = QStringLiteral(SPLIT_ROCK_CODEX_DIR "/codex-acp");
    if (QFileInfo(own).isExecutable())
        return own;
    return QStandardPaths::findExecutable(QStringLiteral("codex-acp"));
}

QString defaultHome()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("split-rock/codex"));
}

Agent codex(const QString &adapter, const QString &home)
{
    Agent a;
    a.name = QStringLiteral("Codex");
    a.program = adapter;
    a.home = home;
    a.environment.insert(QStringLiteral("CODEX_HOME"), home);
    a.signInFile = QStringLiteral("auth.json");
    // Keys are hidden, as is the code typed on another device: the sign-in
    // page in the browser is the one way in.
    a.hiddenMethods = {QStringLiteral("codex-api-key"), QStringLiteral("openai-api-key"), QStringLiteral("api-key"),
                       QStringLiteral("chat-gpt-device-code"), QStringLiteral("gateway")};
    a.askFirstMode = QStringLiteral("read-only");
    a.leftovers = {QStringLiteral("sessions"), QStringLiteral("archived_sessions"),
                   QStringLiteral("history.jsonl"), QStringLiteral("log"),
                   QStringLiteral("state_*.sqlite*"), QStringLiteral("logs_*.sqlite*"),
                   QStringLiteral("memories_*.sqlite*"), QStringLiteral("goals_*.sqlite*")};
    // An adapter in the home folder, as a developer's own build.
    if (!adapter.isEmpty())
        a.readable = {QFileInfo(adapter).absolutePath()};
    if (adapter.isEmpty() || !QFileInfo(adapter).isExecutable())
        a.unavailable = QStringLiteral("Codex isn't installed on this computer.");
    else if (ask::fenceProgram().isEmpty())
        a.unavailable = QStringLiteral("Codex can't be kept to its own folder on this computer, which lacks "
                                       "the sandbox it runs in (bubblewrap).");
    return a;
}

Agent pretend(const QString &program, const QString &script, const QString &home)
{
    Agent a;
    a.name = QStringLiteral("Pretend assistant");
    a.program = program;
    a.arguments = {script};
    a.home = home;
    a.environment.insert(QStringLiteral("SPLIT_ROCK_PRETEND_HOME"), home);
    a.signInFile = QStringLiteral("signed-in");
    a.hiddenMethods = {QStringLiteral("pretend-key")};
    a.askFirstMode = QStringLiteral("ask-first");
    a.leftovers = {QStringLiteral("transcripts")};
    a.readable = {QFileInfo(program).absoluteFilePath(), QFileInfo(script).absoluteFilePath()};
    a.mayRunUnfenced = true;
    if (!QFileInfo::exists(program))
        a.unavailable = QStringLiteral("The pretend assistant was not built.");
    else if (!QFileInfo::exists(script))
        a.unavailable = QStringLiteral("The pretend assistant's script was not found.");
    return a;
}

QList<acp::AuthMethod> offeredSignIns(const Agent &agent, const QList<acp::AuthMethod> &methods)
{
    QList<acp::AuthMethod> offered;
    for (const acp::AuthMethod &method : methods) {
        if (agent.hiddenMethods.contains(method.id) || method.kind != acp::AuthMethod::Kind::Agent
            || method.meta.contains(QStringLiteral("api-key")) || method.meta.contains(QStringLiteral("gateway")))
            continue;
        offered.append(method);
    }
    return offered;
}

acp::LaunchSpec serveLaunch(const Agent &agent, const QProcessEnvironment &base, const QString &work)
{
    QProcessEnvironment env = ask::fencedEnvironment(base);
    for (auto it = agent.environment.begin(); it != agent.environment.end(); ++it)
        env.insert(it.key(), it.value());
    return {agent.program, agent.arguments, env, work};
}

void removeLeftovers(const Agent &agent)
{
    if (agent.home.isEmpty())
        return;
    const QDir home(agent.home);
    if (!home.exists())
        return;
    for (const QString &leftover : agent.leftovers) {
        if (leftover.contains(QLatin1String("..")) || QDir::isAbsolutePath(leftover))
            continue;
        const qsizetype slash = leftover.lastIndexOf(QLatin1Char('/'));
        const QDir dir(slash < 0 ? home.path() : home.filePath(leftover.left(slash)));
        const QString pattern = slash < 0 ? leftover : leftover.mid(slash + 1);
        if (!dir.exists())
            continue;
        const QFileInfoList entries = dir.entryInfoList({pattern}, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            // Never follow a link out of its folder.
            if (entry.isSymLink() || !entry.absoluteFilePath().startsWith(home.absolutePath() + QLatin1Char('/')))
                continue;
            if (entry.isDir())
                QDir(entry.absoluteFilePath()).removeRecursively();
            else
                QFile::remove(entry.absoluteFilePath());
        }
    }
}

} // namespace splitrock::codex
