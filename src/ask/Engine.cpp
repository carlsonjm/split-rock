// SPDX-License-Identifier: GPL-2.0-or-later
#include "ask/Engine.h"

#include "ask/Fence.h"

#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTcpServer>

#include <utility>

namespace splitrock::ask {

Engine::Engine(QObject *parent)
    : QObject(parent)
{
    m_poll.setInterval(400);
    connect(&m_poll, &QTimer::timeout, this, [this] {
        if (m_clock.elapsed() > m_loadLimit) {
            fail(QStringLiteral("The model didn't load in five minutes."));
            return;
        }
        QUrl health = m_url;
        health.setPath(QStringLiteral("/health"));
        QNetworkReply *reply = m_network.get(QNetworkRequest(health));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            if (!m_starting || reply->error() != QNetworkReply::NoError)
                return;
            m_poll.stop();
            m_starting = false;
            m_up = true;
            qInfo().noquote() << QStringLiteral("split-rock-ask: the model is ready after %1 ms").arg(m_clock.elapsed());
            Q_EMIT up();
        });
    });
}

Engine::~Engine()
{
    stop();
}

void Engine::start(const QString &engine, const QString &model, const QString &layers)
{
    if (m_up || m_starting)
        return;
    m_problem.clear();
    if (engine.isEmpty() || !QFileInfo(engine).isExecutable()) {
        fail(QStringLiteral("The engine Ask runs the model with, llama.cpp, isn't installed."));
        return;
    }
    if (model.isEmpty() || !QFileInfo::exists(model)) {
        fail(QStringLiteral("The model for Ask hasn't been downloaded."));
        return;
    }
    const QString fence = fenceProgram();
    if (fence.isEmpty()) {
        fail(QStringLiteral("Ask keeps the model in a sandbox, bubblewrap, which this computer doesn't have."));
        return;
    }
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    const quint16 port = probe.serverPort();
    probe.close();
    m_key = QString::number(QRandomGenerator::system()->generate64(), 36)
        + QString::number(QRandomGenerator::system()->generate64(), 36);
    m_url = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
    m_output.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyRead, this, [this] {
        m_output += m_process->readAll();
        m_output = m_output.right(4000);
    });
    connect(m_process, &QProcess::finished, this, [this](int code) {
        if (QProcess *process = std::exchange(m_process, nullptr))
            process->deleteLater();
        const bool expected = !m_up && !m_starting;
        m_up = false;
        if (!expected)
            fail(QStringLiteral("The model stopped (%1): %2").arg(code).arg(QString::fromUtf8(m_output.right(600))));
    });
    FenceRoom room;
    room.readable = {QFileInfo(model).absoluteFilePath(), QFileInfo(engine).absolutePath()};
    // The graphics chip, for the model to run on.
    room.devices = {QStringLiteral("/dev/dri"), QStringLiteral("/dev/kfd")};
    QProcessEnvironment environment = fencedEnvironment(QProcessEnvironment::systemEnvironment());
    // Newer llama-servers keep each slot's memory in a second cache of their
    // own and copy it back in between requests: on the Mac mini's soak that
    // carried one question's answer into the next. Each slot keeps only its
    // own memory. As settings by name, a server too old to know them still
    // starts.
    environment.insert(QStringLiteral("LLAMA_ARG_CACHE_RAM"), QStringLiteral("0"));
    environment.insert(QStringLiteral("LLAMA_ARG_CTX_CHECKPOINTS"), QStringLiteral("0"));
    // Without a graphics chip Ask can use, the processor does all of it:
    // llama.cpp otherwise still hands some of the work to the chip.
    if (layers == QLatin1String("0")) {
        environment.insert(QStringLiteral("LLAMA_ARG_DEVICE"), QStringLiteral("none"));
        environment.insert(QStringLiteral("GGML_VK_VISIBLE_DEVICES"), QStringLiteral("-1"));
    }
    m_process->setProcessEnvironment(environment);
    m_process->start(fence, fenced(engine,
                                   {QStringLiteral("--model"), model,
                                    QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                                    QStringLiteral("--port"), QString::number(port),
                                    QStringLiteral("--api-key"), m_key,
                                    // Two slots of 8192 each: the sorting, and the answers.
                                    QStringLiteral("--ctx-size"), QStringLiteral("16384"),
                                    QStringLiteral("--parallel"), QStringLiteral("2"),
                                    QStringLiteral("--jinja"),
                                    QStringLiteral("--no-webui"),
                                    QStringLiteral("--n-gpu-layers"), layers},
                                   room));
    m_process->closeWriteChannel();
    waitForHealth();
}

void Engine::use(const QUrl &server)
{
    if (m_up || m_starting)
        return;
    m_problem.clear();
    m_url = server;
    m_key.clear();
    waitForHealth();
}

void Engine::stop()
{
    m_poll.stop();
    m_starting = false;
    m_up = false;
    if (m_process) {
        QProcess *process = m_process;
        m_process = nullptr;
        process->disconnect(this);
        process->terminate();
        if (!process->waitForFinished(3000))
            process->kill();
        process->deleteLater();
    }
}

void Engine::fail(const QString &why)
{
    stop();
    m_problem = why;
    qWarning().noquote() << QStringLiteral("split-rock-ask: ") + why;
    Q_EMIT down(why);
}

void Engine::waitForHealth()
{
    m_starting = true;
    m_clock.start();
    m_poll.start();
}

} // namespace splitrock::ask
