// SPDX-License-Identifier: GPL-2.0-or-later
//
// The pretend assistant. It speaks the Agent Client Protocol on its standard
// input and output and does only what its script says, so Split Rock can be
// tested with no account, no network and nothing on the computer to change.
//
// It reads its script and writes only inside the folder named by
// SPLIT_ROCK_PRETEND_HOME: whether it is signed in, a pretend transcript, and a
// log of every message it received, for tests to read.
//
//   split-rock-pretend-assistant SCRIPT.json              serve the protocol
//   split-rock-pretend-assistant SCRIPT.json --sign-in X  a sign-in run by the
//                                                         client: X is ok, fail
//                                                         or hang
//
// The script is a JSON object:
//   agent             {name, title, version}
//   protocolVersion   the version to answer initialize with (default 1)
//   authMethods       advertised as given
//   signedIn          whether a session starts without signing in
//   authenticate      {methodId: "ok" | "fail" | "hang"}
//   authenticateOpens {methodId: page}: asks the desktop's opener for the
//                     page first, as an agent signing in in the browser does;
//                     only inside Split Rock's fence, so no real browser opens
//   modes             returned with the session, as given
//   sessionError      {code, message, data}: session/new always fails so
//   turns             [{expectPrompt?, steps: [...]}], used in order; a turn
//                     with expectPrompt is used only for a prompt containing it
// Steps: {say}, {think}, {tool: ToolCall}, {update: ToolCallUpdate},
//        {ask: {toolCall, options}, expect: optionKind}, {wait: ms},
//        {waitForCancel: true}, {stop: reason}, {error: {code, message}},
//        {exit: code}, {write: {path, text}}: writes the file itself, as an
//        agent does after asking, and says whether it could; {run: [program,
//        arguments...]}: runs it where the assistant runs, as an agent's own
//        tool does, and says what it printed; {mcp: {server, list: true}} or
//        {mcp: {server, tool, arguments}}: starts the tool server of that
//        name the session was given, as Codex does, and says the tools it
//        lists, or what the call gave ("ERROR: " first when it failed).
// ${HOME} in any step is the home folder the pretend assistant sees.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

QString homePath()
{
    return qEnvironmentVariable("SPLIT_ROCK_PRETEND_HOME");
}

QString signedInMarker()
{
    return homePath() + QStringLiteral("/signed-in");
}

void touch(const QString &path)
{
    if (homePath().isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append))
        file.close();
}

void appendLine(const QString &path, const QByteArray &line)
{
    if (homePath().isEmpty())
        return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write(line);
        file.write("\n");
    }
}

int runSignIn(const QString &outcome)
{
    QTextStream out(stdout);
    out << "Opening browser to sign in...\n"
        << "If the browser didn't open, visit: https://pretend.invalid/sign-in?step=1\n";
    out.flush();
    if (outcome == QLatin1String("ok")) {
        touch(signedInMarker());
        return 0;
    }
    if (outcome == QLatin1String("hang")) {
        // Waits as a sign-in does when nobody finishes it.
        QThread::sleep(3600);
        return 1;
    }
    return 1;
}

class StdinReader : public QThread
{
    Q_OBJECT
public:
    using QThread::QThread;
Q_SIGNALS:
    void line(const QByteArray &line);
    void closed();

protected:
    void run() override
    {
        std::string text;
        while (std::getline(std::cin, text))
            Q_EMIT line(QByteArray::fromStdString(text));
        Q_EMIT closed();
    }
};

class PretendAgent : public QObject
{
    Q_OBJECT
public:
    explicit PretendAgent(const QJsonObject &script)
        : m_script(script)
        , m_turns(script.value(QStringLiteral("turns")).toArray())
    {
    }

    void handle(const QByteArray &line)
    {
        appendLine(homePath() + QStringLiteral("/received.jsonl"), line);
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonValue id = message.value(QStringLiteral("id"));
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();

        if (method.isEmpty()) {
            handleReply(id.toInt(), message);
            return;
        }
        if (method == QLatin1String("initialize")) {
            respond(id, QJsonObject{
                {QStringLiteral("protocolVersion"), m_script.value(QStringLiteral("protocolVersion")).toInt(1)},
                {QStringLiteral("agentInfo"), m_script.value(QStringLiteral("agent"))},
                {QStringLiteral("authMethods"), m_script.value(QStringLiteral("authMethods")).toArray()},
                {QStringLiteral("agentCapabilities"), QJsonObject{{QStringLiteral("loadSession"), false}}},
            });
        } else if (method == QLatin1String("authenticate")) {
            const QString page = m_script.value(QStringLiteral("authenticateOpens")).toObject()
                                     .value(params.value(QStringLiteral("methodId")).toString()).toString();
            if (!page.isEmpty() && qEnvironmentVariableIsSet("SPLIT_ROCK_OPEN_REQUESTS"))
                QProcess::execute(QStringLiteral("xdg-open"), {page});
            const QString outcome = m_script.value(QStringLiteral("authenticate")).toObject()
                                        .value(params.value(QStringLiteral("methodId")).toString()).toString(QStringLiteral("fail"));
            if (outcome == QLatin1String("ok")) {
                touch(signedInMarker());
                respond(id, QJsonObject{});
            } else if (outcome != QLatin1String("hang")) {
                respondError(id, -32603, QStringLiteral("The pretend sign-in failed."));
            }
        } else if (method == QLatin1String("session/new")) {
            const QJsonObject error = m_script.value(QStringLiteral("sessionError")).toObject();
            if (!error.isEmpty()) {
                send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("error"), error}});
            } else if (!m_script.value(QStringLiteral("signedIn")).toBool() && !QFile::exists(signedInMarker())) {
                respondError(id, -32000, QStringLiteral("Authentication required"));
            } else {
                m_sessionId = QStringLiteral("pretend-session-%1").arg(++m_sessionCount);
                m_mcpServers = params.value(QStringLiteral("mcpServers")).toArray();
                QJsonObject result{{QStringLiteral("sessionId"), m_sessionId}};
                if (m_script.contains(QStringLiteral("modes")))
                    result.insert(QStringLiteral("modes"), m_script.value(QStringLiteral("modes")));
                respond(id, result);
            }
        } else if (method == QLatin1String("session/set_mode")) {
            respond(id, QJsonObject{});
        } else if (method == QLatin1String("session/prompt")) {
            startTurn(id, params);
        } else if (method == QLatin1String("session/cancel")) {
            m_cancelled = true;
            if (m_waitingForCancel) {
                m_waitingForCancel = false;
                finishTurn(QStringLiteral("cancelled"));
            }
        } else if (!id.isUndefined() && !id.isNull()) {
            respondError(id, -32601, QStringLiteral("The pretend assistant has no %1.").arg(method));
        }
    }

private:
    void send(const QJsonObject &message)
    {
        std::fputs(QJsonDocument(message).toJson(QJsonDocument::Compact).constData(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

    void respond(const QJsonValue &id, const QJsonValue &result)
    {
        send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}, {QStringLiteral("result"), result}});
    }

    void respondError(const QJsonValue &id, int code, const QString &text)
    {
        send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
              {QStringLiteral("id"), id},
              {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), text}}}});
    }

    void update(const QJsonObject &content)
    {
        send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
              {QStringLiteral("method"), QStringLiteral("session/update")},
              {QStringLiteral("params"), QJsonObject{{QStringLiteral("sessionId"), m_sessionId},
                                                     {QStringLiteral("update"), content}}}});
    }

    void say(const QString &kind, const QString &text)
    {
        update({{QStringLiteral("sessionUpdate"), kind},
                {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                        {QStringLiteral("text"), text}}}});
    }

    void startTurn(const QJsonValue &id, const QJsonObject &params)
    {
        QString text;
        for (const QJsonValue &block : params.value(QStringLiteral("prompt")).toArray())
            text += block.toObject().value(QStringLiteral("text")).toString() + QLatin1Char('\n');
        appendLine(homePath() + QStringLiteral("/transcripts/session.txt"), text.toUtf8());

        m_promptId = id;
        m_cancelled = false;
        m_steps = {};
        for (int i = 0; i < m_turns.size(); ++i) {
            const QJsonObject turn = m_turns.at(i).toObject();
            const QString expected = turn.value(QStringLiteral("expectPrompt")).toString();
            if (expected.isEmpty() || text.contains(expected, Qt::CaseInsensitive)) {
                m_steps = turn.value(QStringLiteral("steps")).toArray();
                m_turns.removeAt(i);
                break;
            }
        }
        if (m_steps.isEmpty())
            m_steps.append(QJsonObject{{QStringLiteral("say"), QStringLiteral("I have nothing scripted for that.")}});
        m_step = 0;
        next();
    }

    void next()
    {
        while (m_step < m_steps.size()) {
            QString text = QString::fromUtf8(QJsonDocument(m_steps.at(m_step++).toObject()).toJson(QJsonDocument::Compact));
            text.replace(QStringLiteral("${HOME}"), QDir::homePath());
            const QJsonObject step = QJsonDocument::fromJson(text.toUtf8()).object();
            if (step.contains(QStringLiteral("say"))) {
                say(QStringLiteral("agent_message_chunk"), step.value(QStringLiteral("say")).toString());
            } else if (step.contains(QStringLiteral("think"))) {
                say(QStringLiteral("agent_thought_chunk"), step.value(QStringLiteral("think")).toString());
            } else if (step.contains(QStringLiteral("tool"))) {
                QJsonObject call = step.value(QStringLiteral("tool")).toObject();
                call.insert(QStringLiteral("sessionUpdate"), QStringLiteral("tool_call"));
                update(call);
            } else if (step.contains(QStringLiteral("update"))) {
                QJsonObject call = step.value(QStringLiteral("update")).toObject();
                call.insert(QStringLiteral("sessionUpdate"), QStringLiteral("tool_call_update"));
                update(call);
            } else if (step.contains(QStringLiteral("ask"))) {
                QJsonObject params = step.value(QStringLiteral("ask")).toObject();
                params.insert(QStringLiteral("sessionId"), m_sessionId);
                m_expect = step.value(QStringLiteral("expect")).toString();
                m_askOptions = params.value(QStringLiteral("options")).toArray();
                m_askId = 1000 + m_step;
                send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                      {QStringLiteral("id"), m_askId},
                      {QStringLiteral("method"), QStringLiteral("session/request_permission")},
                      {QStringLiteral("params"), params}});
                return;
            } else if (step.contains(QStringLiteral("wait"))) {
                QTimer::singleShot(step.value(QStringLiteral("wait")).toInt(), this, &PretendAgent::next);
                return;
            } else if (step.contains(QStringLiteral("waitForCancel"))) {
                if (m_cancelled) {
                    finishTurn(QStringLiteral("cancelled"));
                } else {
                    m_waitingForCancel = true;
                }
                return;
            } else if (step.contains(QStringLiteral("error"))) {
                send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                      {QStringLiteral("id"), m_promptId},
                      {QStringLiteral("error"), step.value(QStringLiteral("error"))}});
                m_promptId = QJsonValue();
                return;
            } else if (step.contains(QStringLiteral("write"))) {
                const QJsonObject write = step.value(QStringLiteral("write")).toObject();
                const QString path = write.value(QStringLiteral("path")).toString();
                QDir().mkpath(QFileInfo(path).absolutePath());
                QFile file(path);
                const bool wrote = file.open(QIODevice::WriteOnly)
                    && file.write(write.value(QStringLiteral("text")).toString().toUtf8()) >= 0;
                file.close();
                say(QStringLiteral("agent_message_chunk"),
                    (wrote ? QStringLiteral("Wrote %1.\n") : QStringLiteral("Could not write %1.\n")).arg(path));
            } else if (step.contains(QStringLiteral("run"))) {
                QStringList words;
                for (const QJsonValue &word : step.value(QStringLiteral("run")).toArray())
                    words.append(word.toString());
                QProcess run;
                run.setProcessChannelMode(QProcess::MergedChannels);
                if (!words.isEmpty())
                    run.start(words.takeFirst(), words);
                run.waitForFinished(10000);
                say(QStringLiteral("agent_message_chunk"), QString::fromUtf8(run.readAll()));
            } else if (step.contains(QStringLiteral("mcp"))) {
                say(QStringLiteral("agent_message_chunk"), mcp(step.value(QStringLiteral("mcp")).toObject()));
            } else if (step.contains(QStringLiteral("exit"))) {
                std::fflush(stdout);
                std::_Exit(step.value(QStringLiteral("exit")).toInt());
            } else if (step.contains(QStringLiteral("stop"))) {
                finishTurn(step.value(QStringLiteral("stop")).toString());
                return;
            }
        }
        finishTurn(m_cancelled ? QStringLiteral("cancelled") : QStringLiteral("end_turn"));
    }

    void handleReply(int id, const QJsonObject &message)
    {
        if (id != m_askId)
            return;
        m_askId = -1;
        const QJsonObject outcome = message.value(QStringLiteral("result")).toObject().value(QStringLiteral("outcome")).toObject();
        QString chosenKind = outcome.value(QStringLiteral("outcome")).toString();
        if (chosenKind == QLatin1String("selected")) {
            const QString optionId = outcome.value(QStringLiteral("optionId")).toString();
            for (const QJsonValue &option : m_askOptions) {
                if (option.toObject().value(QStringLiteral("optionId")).toString() == optionId)
                    chosenKind = option.toObject().value(QStringLiteral("kind")).toString();
            }
        }
        if (!m_expect.isEmpty() && chosenKind != m_expect) {
            say(QStringLiteral("agent_message_chunk"),
                QStringLiteral("UNEXPECTED REPLY: wanted %1, got %2.").arg(m_expect, chosenKind));
            finishTurn(QStringLiteral("refusal"));
            return;
        }
        next();
    }

    void finishTurn(const QString &stopReason)
    {
        if (m_promptId.isUndefined() || m_promptId.isNull())
            return;
        respond(m_promptId, QJsonObject{{QStringLiteral("stopReason"), stopReason}});
        m_promptId = QJsonValue();
        m_steps = {};
    }

    // One tool server the session was given, started on first use, and asked
    // one thing, as Codex asks it.
    QString mcp(const QJsonObject &step)
    {
        const QString name = step.value(QStringLiteral("server")).toString();
        QProcess *server = m_mcp.value(name);
        if (!server) {
            QJsonObject given;
            for (const QJsonValue &each : std::as_const(m_mcpServers)) {
                if (each.toObject().value(QStringLiteral("name")).toString() == name)
                    given = each.toObject();
            }
            if (given.isEmpty())
                return QStringLiteral("ERROR: no tool server called %1.").arg(name);
            server = new QProcess(this);
            server->setProcessChannelMode(QProcess::ForwardedErrorChannel);
            QStringList args;
            for (const QJsonValue &arg : given.value(QStringLiteral("args")).toArray())
                args.append(arg.toString());
            server->start(given.value(QStringLiteral("command")).toString(), args);
            if (!server->waitForStarted(5000))
                return QStringLiteral("ERROR: the tool server didn't start.");
            m_mcp.insert(name, server);
            mcpAsk(server, QStringLiteral("initialize"), QJsonObject{{QStringLiteral("protocolVersion"), QStringLiteral("2025-06-18")},
                                                                     {QStringLiteral("capabilities"), QJsonObject{}},
                                                                     {QStringLiteral("clientInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("pretend")}}}});
            server->write("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n");
        }
        if (step.value(QStringLiteral("list")).toBool()) {
            QStringList names;
            for (const QJsonValue &tool : mcpAsk(server, QStringLiteral("tools/list"), {}).value(QStringLiteral("result")).toObject()
                                              .value(QStringLiteral("tools")).toArray())
                names.append(tool.toObject().value(QStringLiteral("name")).toString());
            return QStringLiteral("Tools: %1\n").arg(names.join(QStringLiteral(", ")));
        }
        const QJsonObject reply = mcpAsk(server, QStringLiteral("tools/call"),
                                         QJsonObject{{QStringLiteral("name"), step.value(QStringLiteral("tool"))},
                                                     {QStringLiteral("arguments"), step.value(QStringLiteral("arguments"))}});
        const QJsonObject result = reply.value(QStringLiteral("result")).toObject();
        QString text;
        for (const QJsonValue &block : result.value(QStringLiteral("content")).toArray())
            text += block.toObject().value(QStringLiteral("text")).toString();
        if (reply.isEmpty())
            return QStringLiteral("ERROR: the tool server didn't answer.\n");
        return (result.value(QStringLiteral("isError")).toBool() ? QStringLiteral("ERROR: ") : QString()) + text + QLatin1Char('\n');
    }

    QJsonObject mcpAsk(QProcess *server, const QString &method, const QJsonObject &params)
    {
        const int id = ++m_mcpId;
        server->write(QJsonDocument(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id},
                                                {QStringLiteral("method"), method}, {QStringLiteral("params"), params}})
                          .toJson(QJsonDocument::Compact) + '\n');
        while (!server->canReadLine()) {
            if (!server->waitForReadyRead(10000))
                return {};
        }
        return QJsonDocument::fromJson(server->readLine()).object();
    }

    QJsonObject m_script;
    QJsonArray m_turns;
    QJsonArray m_mcpServers;
    QHash<QString, QProcess *> m_mcp;
    int m_mcpId = 0;
    QString m_sessionId;
    int m_sessionCount = 0;
    QJsonValue m_promptId;
    QJsonArray m_steps;
    int m_step = 0;
    int m_askId = -1;
    QString m_expect;
    QJsonArray m_askOptions;
    bool m_cancelled = false;
    bool m_waitingForCancel = false;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        std::fputs("usage: split-rock-pretend-assistant SCRIPT.json [--sign-in ok|fail|hang]\n", stderr);
        return 2;
    }
    const int signIn = args.indexOf(QStringLiteral("--sign-in"));
    if (signIn > 0)
        return runSignIn(args.value(signIn + 1));

    QFile file(args.at(1));
    if (!file.open(QIODevice::ReadOnly)) {
        std::fputs("the pretend assistant could not read its script\n", stderr);
        return 2;
    }
    const QJsonObject script = QJsonDocument::fromJson(file.readAll()).object();

    PretendAgent agent(script);
    StdinReader reader;
    QObject::connect(&reader, &StdinReader::line, &agent, &PretendAgent::handle, Qt::QueuedConnection);
    QObject::connect(&reader, &StdinReader::closed, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    reader.start();
    const int code = app.exec();
    // The reader is blocked in getline only when stdin is still open, which it
    // is not once closed() has arrived.
    reader.wait(1000);
    return code;
}

#include "main.moc"
