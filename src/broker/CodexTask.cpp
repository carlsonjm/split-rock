// SPDX-License-Identifier: GPL-2.0-or-later
#include "broker/CodexTask.h"

#include "acp/Client.h"
#include "ask/Fence.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>

namespace splitrock::broker {

CodexTask::CodexTask(const codex::Agent &agent, QObject *parent)
    : QObject(parent)
    , m_agent(agent)
{
}

CodexTask::~CodexTask()
{
    if (acp::Client *client = m_client.data()) {
        client->disconnect(this);
        client->kill();
        client->deleteLater();
    }
    m_work.reset();
    codex::removeLeftovers(m_agent);
}

void CodexTask::start(const QString &prompt)
{
    m_prompt = prompt;
    if (!m_agent.unavailable.isEmpty()) {
        end(m_agent.unavailable);
        return;
    }
    if (!m_agent.signedIn()) {
        end(QStringLiteral("Codex isn't signed in. Sign in from Settings, under Shuffle Core."));
        return;
    }
    codex::removeLeftovers(m_agent);
    QDir().mkpath(m_agent.home);
    m_work = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/split-rock-broker-XXXXXX"));
    if (!m_work->isValid()) {
        end(QStringLiteral("Codex couldn't be given a folder of its own."));
        return;
    }
    acp::LaunchSpec launch = codex::serveLaunch(m_agent, QProcessEnvironment::systemEnvironment(), m_work->path());
    const QString fence = ask::fenceProgram();
    if (!fence.isEmpty()) {
        // Only its own folder and an empty working folder: nothing of the
        // person's, not even the desktop's settings, since it needs none to
        // write words.
        ask::FenceRoom room;
        room.readable = m_agent.readable;
        room.writable = {m_agent.home, m_work->path()};
        launch.arguments = ask::fenced(launch.program, launch.arguments, room);
        launch.program = fence;
    } else if (!m_agent.mayRunUnfenced) {
        end(QStringLiteral("Codex can't be kept to its own folder on this computer, which lacks the sandbox it "
                           "runs in (bubblewrap)."));
        return;
    }
    m_client = new acp::Client(this);
    connect(m_client, &acp::Client::stopped, this, [this](const QString &, bool expected) {
        if (!expected)
            end(QStringLiteral("Codex stopped."));
    });
    connect(m_client, &acp::Client::messageChunk, this, [this](const QString &text) {
        m_text += text;
        Q_EMIT said(text);
    });
    m_client->start(launch);
    m_client->initialize(QStringLiteral(PROJECT_VERSION), QStringLiteral("Shuffle Core"),
                         [this](const std::optional<acp::RpcError> &error) {
        if (error) {
            end(QStringLiteral("Codex didn't start: %1").arg(error->said()));
            return;
        }
        session();
    });
}

void CodexTask::session()
{
    if (!m_client)
        return;
    m_client->newSession(m_work->path(), [this](const std::optional<acp::RpcError> &error) {
        if (error && error->code == acp::AuthRequiredCode) {
            end(QStringLiteral("Codex isn't signed in. Sign in from Settings, under Shuffle Core."));
            return;
        }
        if (error) {
            end(QStringLiteral("Codex couldn't start: %1").arg(error->said()));
            return;
        }
        bool offered = false;
        for (const acp::Client::SessionMode &mode : m_client->modes())
            offered = offered || mode.id == m_agent.askFirstMode;
        if (!offered || m_client->currentMode() == m_agent.askFirstMode) {
            ask();
            return;
        }
        m_client->setMode(m_agent.askFirstMode, [this](const std::optional<acp::RpcError> &) { ask(); });
    });
}

void CodexTask::ask()
{
    if (!m_client)
        return;
    const QJsonArray blocks{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                        {QStringLiteral("text"), m_prompt}}};
    m_client->prompt(blocks, [this](const QString &stopReason, const std::optional<acp::RpcError> &error) {
        if (error && error->code == acp::AuthRequiredCode)
            end(QStringLiteral("Codex isn't signed in. Sign in from Settings, under Shuffle Core."));
        else if (error && error->code == acp::AgentStoppedCode)
            end(QStringLiteral("Codex stopped."));
        else if (error)
            end(QStringLiteral("Codex couldn't answer. %1").arg(error->said()));
        else if (stopReason == QLatin1String("refusal"))
            end(QStringLiteral("Codex would not answer that."));
        else if (stopReason == QLatin1String("cancelled"))
            end(QStringLiteral("cancelled"));
        else
            end({});
    });
}

void CodexTask::cancel()
{
    if (m_client)
        m_client->cancel();
    end(QStringLiteral("cancelled"));
}

void CodexTask::end(const QString &problem)
{
    if (m_done)
        return;
    m_done = true;
    if (acp::Client *client = m_client.data()) {
        m_client.clear();
        client->disconnect(this);
        client->kill();
        client->deleteLater();
    }
    m_work.reset();
    codex::removeLeftovers(m_agent);
    Q_EMIT finished(m_text.trimmed(), problem);
}

} // namespace splitrock::broker
