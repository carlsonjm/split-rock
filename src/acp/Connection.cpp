// SPDX-License-Identifier: GPL-2.0-or-later
#include "acp/Connection.h"

#include <QJsonDocument>
#include <QPointer>
#include <QTimer>

namespace acp {

Connection::Connection(QObject *parent)
    : QObject(parent)
{
}

Connection::~Connection()
{
    kill();
}

void Connection::kill()
{
    // Handlers may point at objects that are being torn down, so they are
    // dropped rather than called.
    m_pending.clear();
    if (!m_process)
        return;
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(1500)) {
            m_process->kill();
            m_process->waitForFinished(1500);
        }
    }
    m_reportedStop = true;
}

void Connection::start(const LaunchSpec &spec)
{
    Q_ASSERT(!m_process);
    m_process = new QProcess(this);
    m_process->setProgram(spec.program);
    m_process->setArguments(spec.arguments);
    m_process->setProcessEnvironment(spec.environment);
    m_process->setWorkingDirectory(spec.workingDirectory);

    connect(m_process, &QProcess::started, this, &Connection::started);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Connection::readOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errorBuffer += m_process->readAllStandardError();
        qsizetype end;
        while ((end = m_errorBuffer.indexOf('\n')) >= 0) {
            Q_EMIT diagnostic(QString::fromUtf8(m_errorBuffer.left(end)));
            m_errorBuffer.remove(0, end + 1);
        }
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_reportedStop)
            return;
        m_reportedStop = true;
        failPending(QStringLiteral("The assistant's program did not start."));
        Q_EMIT stopped(m_process->errorString(), m_stopping);
    });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        readOutput();
        if (m_reportedStop)
            return;
        m_reportedStop = true;
        failPending(QStringLiteral("The assistant's program stopped."));
        const QString reason = status == QProcess::CrashExit
            ? QStringLiteral("The assistant's program crashed.")
            : QStringLiteral("The assistant's program ended with code %1.").arg(exitCode);
        Q_EMIT stopped(reason, m_stopping);
    });

    m_process->start(QIODevice::ReadWrite);
}

void Connection::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    m_stopping = true;
    m_process->closeWriteChannel();
    m_process->terminate();
    QPointer<QProcess> process = m_process;
    QTimer::singleShot(3000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}

bool Connection::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

int Connection::request(const QString &method, const QJsonObject &params, ReplyHandler handler)
{
    const int id = m_nextId++;
    m_pending.insert(id, std::move(handler));
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
          {QStringLiteral("id"), id},
          {QStringLiteral("method"), method},
          {QStringLiteral("params"), params}});
    return id;
}

void Connection::notify(const QString &method, const QJsonObject &params)
{
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
          {QStringLiteral("method"), method},
          {QStringLiteral("params"), params}});
}

void Connection::respond(const QJsonValue &id, const QJsonValue &result)
{
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
          {QStringLiteral("id"), id},
          {QStringLiteral("result"), result}});
}

void Connection::respondError(const QJsonValue &id, int code, const QString &message)
{
    send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
          {QStringLiteral("id"), id},
          {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                                {QStringLiteral("message"), message}}}});
}

void Connection::send(const QJsonObject &message)
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_process->write(line);
}

void Connection::readOutput()
{
    if (!m_process)
        return;
    m_buffer += m_process->readAllStandardOutput();
    qsizetype end;
    while ((end = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(end).trimmed();
        m_buffer.remove(0, end + 1);
        if (!line.isEmpty())
            handleLine(line);
    }
}

void Connection::handleLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        // Some programs print to standard output before the protocol starts.
        Q_EMIT diagnostic(QStringLiteral("not a protocol message: ") + QString::fromUtf8(line.left(200)));
        return;
    }
    const QJsonObject message = document.object();
    const QString method = message.value(QStringLiteral("method")).toString();
    const bool hasId = message.contains(QStringLiteral("id")) && !message.value(QStringLiteral("id")).isNull();

    if (!method.isEmpty()) {
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        if (hasId)
            Q_EMIT requestReceived(message.value(QStringLiteral("id")), method, params);
        else
            Q_EMIT notificationReceived(method, params);
        return;
    }

    if (!hasId)
        return;
    const int id = message.value(QStringLiteral("id")).toInt(-1);
    const ReplyHandler handler = m_pending.take(id);
    if (!handler)
        return;
    if (message.contains(QStringLiteral("error"))) {
        const QJsonObject error = message.value(QStringLiteral("error")).toObject();
        handler({}, RpcError{error.value(QStringLiteral("code")).toInt(),
                             error.value(QStringLiteral("message")).toString(),
                             error.value(QStringLiteral("data"))});
    } else {
        handler(message.value(QStringLiteral("result")), std::nullopt);
    }
}

void Connection::failPending(const QString &message)
{
    const auto pending = std::exchange(m_pending, {});
    for (const ReplyHandler &handler : pending)
        handler({}, RpcError{AgentStoppedCode, message, {}});
}

} // namespace acp
