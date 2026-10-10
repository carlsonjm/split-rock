// SPDX-License-Identifier: GPL-2.0-or-later
//
// Genie on this computer: the assistant that needs no account. It speaks the
// Agent Client Protocol on its standard input and output, as every assistant
// Split Rock offers does, and runs in the same fence. Behind it are llama.cpp's
// llama-server, started here, inside the fence, and the model Split Rock
// downloaded; in front of the model is the front desk (FrontDesk.h).
//
// Split Rock tells it, in the environment:
//   SPLIT_ROCK_DESK_ENGINE   llama-server
//   SPLIT_ROCK_DESK_MODEL    the model file
//   SPLIT_ROCK_DESK_LAYERS   how many of the model's layers go to the
//                            graphics chip: 99 for all, 0 for none
//   SPLIT_ROCK_DESK_SERVER   instead of the three above, a llama-server
//                            already running, for tests and trials
//
// With --sorting it prints the words and shape the model sorts asks with,
// for the framing block on standard input, and exits (tools/desk-trial).
//
// What it hands Split Rock goes through the fence's stand-ins:
// split-rock-skill to open a settings skill, split-rock-change to propose.

#include "desk/FrontDesk.h"
#include "desk/Model.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <iostream>
#include <string>

using namespace splitrock;
using namespace splitrock::desk;

namespace {

void log(const QString &line)
{
    std::fputs(qPrintable(QStringLiteral("split-rock-desk: ") + line + QLatin1Char('\n')), stderr);
    std::fflush(stderr);
}

// What this computer runs, from os-release, which the fence leaves readable.
QString facts()
{
    QFile file(QStringLiteral("/etc/os-release"));
    QString name, like;
    if (file.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : file.readAll().split('\n')) {
            const QString text = QString::fromUtf8(line);
            const auto value = [&text] { return text.section(QLatin1Char('='), 1).remove(QLatin1Char('"')); };
            if (text.startsWith(QLatin1String("PRETTY_NAME=")))
                name = value();
            else if (text.startsWith(QLatin1String("ID_LIKE=")))
                like = value();
        }
    }
    if (name.isEmpty())
        return {};
    QString words = QStringLiteral("This computer runs %1").arg(name);
    if (like.contains(QLatin1String("arch")) || name.contains(QLatin1String("Arch")))
        words += QStringLiteral(", which is based on Arch Linux, with KDE Plasma 6. Software comes from Discover, "
                                "Plasma's app store; Ubuntu's apt does not exist here.");
    else
        words += QStringLiteral(", with KDE Plasma 6. Software comes from Discover, Plasma's app store.");
    return words;
}

// What Split Rock's framing block says: the shelf, the upkeep cards and who
// takes a bigger change, with the computer's facts read here.
Context contextFrom(const QJsonObject &meta)
{
    Context context;
    for (const QJsonValue &value : meta.value(QStringLiteral("skills")).toArray()) {
        const QJsonObject object = value.toObject();
        Skill skill;
        skill.id = object.value(QStringLiteral("id")).toString();
        skill.title = object.value(QStringLiteral("title")).toString();
        for (const QJsonValue &also : object.value(QStringLiteral("also")).toArray())
            skill.also.append(also.toString());
        if (!skill.id.isEmpty())
            context.skills.append(skill);
    }
    for (const QJsonValue &value : meta.value(QStringLiteral("cards")).toArray()) {
        const QJsonObject object = value.toObject();
        context.cards.append({object.value(QStringLiteral("name")).toString(), object.value(QStringLiteral("what")).toString()});
    }
    context.handOffTo = meta.value(QStringLiteral("handOffTo")).toString();
    context.facts = facts();
    return context;
}

class StandIns : public Tools
{
public:
    void openSkill(const QString &id, const std::function<void(const QString &)> &done) override
    {
        auto *process = new QProcess;
        process->setProcessChannelMode(QProcess::SeparateChannels);
        QObject::connect(process, &QProcess::finished, process, [process, done] {
            done(QString::fromUtf8(process->readAllStandardOutput()));
            process->deleteLater();
        });
        QObject::connect(process, &QProcess::errorOccurred, process, [process, done](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart)
                return;
            done({});
            process->deleteLater();
        });
        process->start(QStringLiteral("split-rock-skill"), {id});
        process->closeWriteChannel();
    }

    bool propose(const QJsonObject &proposal) override
    {
        QProcess process;
        process.start(QStringLiteral("split-rock-change"),
                      {QString::fromUtf8(QJsonDocument(proposal).toJson(QJsonDocument::Compact))});
        process.closeWriteChannel();
        return process.waitForFinished(5000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    }
};

// llama-server, started inside the fence and stopped with this program.
class Engine : public QObject
{
    Q_OBJECT
public:
    QUrl url;
    QString key;
    bool ready = false;
    QString problem;

    void start()
    {
        const QString given = qEnvironmentVariable("SPLIT_ROCK_DESK_SERVER");
        if (!given.isEmpty()) {
            url = QUrl(given);
            waitForHealth();
            return;
        }
        const QString engine = qEnvironmentVariable("SPLIT_ROCK_DESK_ENGINE");
        const QString model = qEnvironmentVariable("SPLIT_ROCK_DESK_MODEL");
        if (engine.isEmpty() || !QFile::exists(engine)) {
            fail(QStringLiteral("The engine for Genie on this computer isn't installed."));
            return;
        }
        if (model.isEmpty() || !QFile::exists(model)) {
            fail(QStringLiteral("The model for Genie on this computer hasn't been downloaded."));
            return;
        }
        // A free port on this computer only, and a key only this program
        // knows, so no other program on the computer can use the model.
        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        const quint16 port = probe.serverPort();
        probe.close();
        key = QString::number(QRandomGenerator::system()->generate64(), 36)
            + QString::number(QRandomGenerator::system()->generate64(), 36);
        url = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_process, &QProcess::readyRead, this, [this] {
            m_output += m_process->readAll();
            m_output = m_output.right(4000);
        });
        connect(m_process, &QProcess::finished, this, [this](int code) {
            if (!ready || code != 0)
                fail(QStringLiteral("The model stopped (%1): %2").arg(code).arg(QString::fromUtf8(m_output.right(600))));
            ready = false;
        });
        const QString layers = qEnvironmentVariable("SPLIT_ROCK_DESK_LAYERS", QStringLiteral("0"));
        m_process->start(engine, {QStringLiteral("--model"), model,
                                  QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                                  QStringLiteral("--port"), QString::number(port),
                                  QStringLiteral("--api-key"), key,
                                  // Two slots of 8192 each: the sorting, and the answers.
                                  QStringLiteral("--ctx-size"), QStringLiteral("16384"),
                                  QStringLiteral("--parallel"), QStringLiteral("2"),
                                  QStringLiteral("--jinja"),
                                  QStringLiteral("--no-webui"),
                                  QStringLiteral("--n-gpu-layers"), layers});
        m_process->closeWriteChannel();
        waitForHealth();
    }

Q_SIGNALS:
    void up();
    void down(const QString &problem);

private:
    void fail(const QString &why)
    {
        problem = why;
        log(why);
        Q_EMIT down(why);
    }

    void waitForHealth()
    {
        m_clock.start();
        auto *timer = new QTimer(this);
        timer->setInterval(400);
        connect(timer, &QTimer::timeout, this, [this, timer] {
            if (!problem.isEmpty()) {
                timer->deleteLater();
                return;
            }
            if (m_clock.elapsed() > 300000) {
                timer->deleteLater();
                fail(QStringLiteral("The model didn't load in five minutes."));
                return;
            }
            QUrl health = url;
            health.setPath(QStringLiteral("/health"));
            QNetworkRequest request(health);
            QNetworkReply *reply = m_network.get(request);
            connect(reply, &QNetworkReply::finished, this, [this, reply, timer] {
                reply->deleteLater();
                if (ready || reply->error() != QNetworkReply::NoError)
                    return;
                ready = true;
                timer->deleteLater();
                log(QStringLiteral("the model is ready after %1 ms").arg(m_clock.elapsed()));
                Q_EMIT up();
            });
        });
        timer->start();
    }

    QProcess *m_process = nullptr;
    QByteArray m_output;
    QNetworkAccessManager m_network;
    QElapsedTimer m_clock;
};

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

class Agent : public QObject
{
    Q_OBJECT
public:
    Agent()
    {
        connect(&m_engine, &Engine::up, this, [this] {
            m_model = new Model(m_engine.url, m_engine.key, this);
            m_desk = new FrontDesk(m_model, &m_tools, this);
            m_desk->setContext(m_context);
            connect(m_desk, &FrontDesk::said, this, [this](const QString &text) { say(text); });
            connect(m_desk, &FrontDesk::routed, this, [](const QString &how) { log(QStringLiteral("routed: ") + how); });
            connect(m_desk, &FrontDesk::finished, this, [this](bool handOff) { finishTurn(QStringLiteral("end_turn"), handOff); });
            if (m_waiting.isEmpty())
                m_desk->warm();
            else
                take(std::exchange(m_waiting, {}));
        });
        connect(&m_engine, &Engine::down, this, [this](const QString &why) {
            if (m_promptId.isUndefined() || m_promptId.isNull())
                return;
            say(QStringLiteral("Genie on this computer couldn't start. %1").arg(why));
            finishTurn(QStringLiteral("end_turn"), false);
        });
    }

    void handle(const QByteArray &line)
    {
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        const QString method = message.value(QStringLiteral("method")).toString();
        const QJsonValue id = message.value(QStringLiteral("id"));
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        if (method == QLatin1String("initialize")) {
            respond(id, QJsonObject{
                {QStringLiteral("protocolVersion"), 1},
                {QStringLiteral("agentInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("split-rock-desk")},
                                                          {QStringLiteral("title"), QStringLiteral("Genie on this computer")},
                                                          {QStringLiteral("version"), QStringLiteral(PROJECT_VERSION)}}},
                {QStringLiteral("authMethods"), QJsonArray{}},
                {QStringLiteral("agentCapabilities"), QJsonObject{{QStringLiteral("loadSession"), false}}},
            });
        } else if (method == QLatin1String("session/new")) {
            m_sessionId = QStringLiteral("desk-1");
            respond(id, QJsonObject{{QStringLiteral("sessionId"), m_sessionId}});
            // The model loads while the person types.
            if (!m_started) {
                m_started = true;
                m_engine.start();
            }
        } else if (method == QLatin1String("session/prompt")) {
            m_promptId = id;
            const QJsonArray blocks = params.value(QStringLiteral("prompt")).toArray();
            if (!m_engine.problem.isEmpty()) {
                say(QStringLiteral("Genie on this computer couldn't start. %1").arg(m_engine.problem));
                finishTurn(QStringLiteral("end_turn"), false);
            } else if (!m_desk) {
                m_waiting = blocks;
            } else {
                take(blocks);
            }
        } else if (method == QLatin1String("session/cancel")) {
            m_waiting = {};
            if (m_desk)
                m_desk->cancel();
            finishTurn(QStringLiteral("cancelled"), false);
        } else if (!method.isEmpty() && !id.isUndefined() && !id.isNull()) {
            respondError(id, -32601, QStringLiteral("Genie on this computer has no %1.").arg(method));
        }
    }

private:
    // One prompt: what Split Rock labelled each block with, then the ask.
    void take(const QJsonArray &blocks)
    {
        QString question;
        QJsonObject report;
        bool labelled = false;
        for (const QJsonValue &value : blocks) {
            const QJsonObject block = value.toObject();
            const QString text = block.value(QStringLiteral("text")).toString();
            const QJsonObject meta = block.value(QStringLiteral("_meta")).toObject().value(QStringLiteral("splitRock")).toObject();
            const QString kind = meta.value(QStringLiteral("kind")).toString();
            labelled = labelled || !kind.isEmpty();
            if (kind == QLatin1String("framing")) {
                const QString memory = m_context.memory;
                m_context = contextFrom(meta);
                m_context.memory = memory;
            } else if (kind == QLatin1String("memory")) {
                m_context.memory = text;
            } else if (kind == QLatin1String("report")) {
                report = meta;
            } else if (kind == QLatin1String("question") || kind.isEmpty()) {
                question = text;
            }
        }
        m_desk->setContext(m_context);
        if (!report.isEmpty()) {
            m_desk->report(report);
            return;
        }
        if (question.trimmed().isEmpty()) {
            finishTurn(QStringLiteral("end_turn"), false);
            return;
        }
        if (!labelled)
            log(QStringLiteral("the prompt carried no labels; taking its last block as the question"));
        m_desk->ask(question.trimmed());
    }

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

    void say(const QString &text)
    {
        send({{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
              {QStringLiteral("method"), QStringLiteral("session/update")},
              {QStringLiteral("params"),
               QJsonObject{{QStringLiteral("sessionId"), m_sessionId},
                           {QStringLiteral("update"),
                            QJsonObject{{QStringLiteral("sessionUpdate"), QStringLiteral("agent_message_chunk")},
                                        {QStringLiteral("content"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                                                {QStringLiteral("text"), text}}}}}}}});
    }

    void finishTurn(const QString &stopReason, bool handOff)
    {
        if (m_promptId.isUndefined() || m_promptId.isNull())
            return;
        QJsonObject result{{QStringLiteral("stopReason"), stopReason}};
        if (handOff)
            result.insert(QStringLiteral("_meta"), QJsonObject{{QStringLiteral("splitRock"),
                                                                QJsonObject{{QStringLiteral("handOff"), true}}}});
        respond(m_promptId, result);
        m_promptId = QJsonValue();
    }

    Engine m_engine;
    StandIns m_tools;
    Model *m_model = nullptr;
    FrontDesk *m_desk = nullptr;
    Context m_context;
    QString m_sessionId;
    QJsonValue m_promptId;
    QJsonArray m_waiting;
    bool m_started = false;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // For trials: the sorting's words and shape for a framing block read on
    // standard input, so a trial can ask the model to sort with the safety
    // check left out and measure the model alone.
    if (app.arguments().contains(QStringLiteral("--sorting"))) {
        std::string text, line;
        while (std::getline(std::cin, line))
            text += line + '\n';
        Model model{QUrl(), QString()};
        StandIns tools;
        FrontDesk desk(&model, &tools);
        desk.setContext(contextFrom(QJsonDocument::fromJson(QByteArray::fromStdString(text)).object()));
        const QJsonObject sorting{{QStringLiteral("words"), desk.sortingWords()},
                                  {QStringLiteral("shape"), desk.sortingShape()}};
        std::fputs(QJsonDocument(sorting).toJson().constData(), stdout);
        return 0;
    }
    Agent agent;
    StdinReader reader;
    QObject::connect(&reader, &StdinReader::line, &agent, &Agent::handle, Qt::QueuedConnection);
    QObject::connect(&reader, &StdinReader::closed, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    reader.start();
    const int code = app.exec();
    reader.wait(1000);
    return code;
}

#include "main.moc"
