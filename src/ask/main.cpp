// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ask on this computer (docs/ASK.md): plain questions answered on the computer
// by the front desk in front of a small model, for Search and the other
// surfaces, on the session bus beside Core. The bus starts it on the first
// call, and it ends on its own after a while with nothing to answer, which
// gives the model's memory back. Core itself never calls a model.
//
//   split-rock-ask [--settings FILE] [--models DIR] [--server URL] [--idle MINUTES]
//   split-rock-ask --framing
//   split-rock-ask --trial --server URL
//
// --settings  the settings file where Ask is switched off
//             (default: split-rock-corerc in the person's settings folder)
// --models    where the model is kept (default: split-rock/models in the
//             person's data folder)
// --server    a llama-server already running, instead of starting one
// --idle      minutes with nothing to answer before it ends (default: 10)
// --framing   prints the words and shape the model sorts asks with, and the
//             Helpers it may name, then ends (tools/desk-trial)
// --trial     answers the trial's asks, one JSON object a line on standard
//             input, against --server, and prints how each went; the skills'
//             look-around comes from the trial's pretend computer, and nothing
//             is proposed anywhere (tools/desk-trial)
//
// Ask runs once: a second start ends at once.

#include "ask/Ask.h"
#include "ask/AskService.h"
#include "ask/LocalModel.h"
#include "codex/Agent.h"
#include "desk/Model.h"
#include "guide/GuideLibrary.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>
#include <iostream>
#include <string>

using namespace splitrock;

namespace {

// llama-server: Shuffle's own build first, then one the person installed.
QString findEngine()
{
    const QString own = QStringLiteral(SPLIT_ROCK_ENGINE_DIR "/llama-server");
    if (QFileInfo(own).isExecutable())
        return own;
    return QStandardPaths::findExecutable(QStringLiteral("llama-server"));
}

// Beside the program in a build, where it was installed otherwise.
QString beside(const QString &name, const QString &installed)
{
    const QDir here(QCoreApplication::applicationDirPath());
    return here.exists(name) ? here.filePath(name) : installed;
}

// The settings skills: installed for every person, and a checkout's own when
// the program runs from one.
QStringList skillRoots()
{
    QStringList roots = GuideLibrary::systemRoots();
    const QDir checkout(QCoreApplication::applicationDirPath() + QStringLiteral("/.."));
    if (checkout.exists(QStringLiteral("skills/plasma")))
        roots.prepend(checkout.absolutePath());
    return roots;
}

void print(const QJsonObject &object)
{
    std::fputs(QJsonDocument(object).toJson(QJsonDocument::Compact).constData(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// The trial's stand-in for the computer: each skill opens with what the
// pretend computer reads, and proposals are only recorded.
class TrialTools : public desk::Tools
{
public:
    QHash<QString, QString> seen;
    QList<Skill> skills;
    QJsonArray proposals;

    void openSkill(const QString &id, const std::function<void(const QString &)> &done) override
    {
        for (const Skill &skill : skills) {
            if (skill.id == id) {
                done(QStringLiteral("# %1\n\n%2\n\n## On this computer now\n\n%3\n")
                         .arg(skill.title, skill.text.trimmed(), seen.value(id, QStringLiteral("(nothing was read)"))));
                return;
            }
        }
        done(QStringLiteral("There is no settings skill called \"%1\".").arg(id));
    }

    bool propose(const QJsonObject &proposal) override
    {
        proposals.append(proposal);
        return true;
    }
};

int trial(ask::Ask &ask, const QUrl &server)
{
    desk::Model model(server, QString());
    const desk::Context context = ask.context();
    std::string line;
    while (std::getline(std::cin, line)) {
        const QJsonObject asked = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        TrialTools tools;
        tools.skills = context.skills;
        const QJsonObject world = asked.value(QStringLiteral("world")).toObject();
        for (auto it = world.begin(); it != world.end(); ++it)
            tools.seen.insert(it.key(), it.value().toString());
        desk::FrontDesk frontDesk(&model, &tools);
        frontDesk.setContext(context);
        QString said, routed;
        bool handOff = false;
        double first = -1;
        QElapsedTimer clock;
        QEventLoop loop;
        QObject::connect(&frontDesk, &desk::FrontDesk::said, &loop, [&](const QString &text) {
            if (first < 0)
                first = clock.elapsed() / 1000.0;
            said += text;
        });
        QObject::connect(&frontDesk, &desk::FrontDesk::routed, &loop, [&](const QString &how) { routed = how; });
        QObject::connect(&frontDesk, &desk::FrontDesk::finished, &loop, [&](bool hands) {
            handOff = hands;
            loop.quit();
        });
        const QJsonArray asks = asked.value(QStringLiteral("asks")).toArray();
        for (qsizetype i = 0; i < asks.size(); ++i) {
            // Only the last ask is measured; those before it are the
            // conversation it follows.
            said.clear();
            routed.clear();
            first = -1;
            tools.proposals = {};
            clock.start();
            frontDesk.ask(asks.at(i).toString());
            if (frontDesk.busy())
                loop.exec();
        }
        print({{QStringLiteral("answer"), said},
               {QStringLiteral("handoff"), handOff},
               {QStringLiteral("routed"), routed},
               {QStringLiteral("cards"), tools.proposals},
               {QStringLiteral("first"), first < 0 ? QJsonValue() : QJsonValue(first)},
               {QStringLiteral("seconds"), clock.elapsed() / 1000.0}});
    }
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("split-rock-ask"));
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Ask on this computer: plain questions answered on the computer."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption settingsOption(QStringLiteral("settings"), QStringLiteral("The settings file."),
                                            QStringLiteral("FILE"));
    const QCommandLineOption modelsOption(QStringLiteral("models"), QStringLiteral("Where the model is kept."),
                                          QStringLiteral("DIR"));
    const QCommandLineOption serverOption(QStringLiteral("server"), QStringLiteral("A llama-server already running."),
                                          QStringLiteral("URL"));
    const QCommandLineOption idleOption(QStringLiteral("idle"),
                                        QStringLiteral("Minutes with nothing to answer before it ends."),
                                        QStringLiteral("MINUTES"), QStringLiteral("10"));
    const QCommandLineOption framingOption(QStringLiteral("framing"), QStringLiteral("Print the sorting's words and end."));
    const QCommandLineOption trialOption(QStringLiteral("trial"), QStringLiteral("Answer the trial's asks from standard input."));
    parser.addOptions({settingsOption, modelsOption, serverOption, idleOption, framingOption, trialOption});
    parser.process(app);

    const QString models = parser.isSet(modelsOption)
        ? QDir(parser.value(modelsOption)).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/split-rock/models");
    const QString engine = findEngine();
    LocalModel model(engine, beside(QStringLiteral("models.json"), QStringLiteral(SPLIT_ROCK_INSTALLED_MODELS)), models);

    ask::Ask::Setup setup;
    setup.engine = engine;
    setup.server = parser.value(serverOption);
    setup.settings = parser.isSet(settingsOption)
        ? parser.value(settingsOption)
        : QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/split-rock-corerc");
    setup.skillRoots = skillRoots();
    setup.codex = codex::codex();
    ask::Ask asking(&model, setup);

    if (parser.isSet(framingOption)) {
        std::fputs(QJsonDocument(asking.framing()).toJson().constData(), stdout);
        return 0;
    }
    if (parser.isSet(trialOption)) {
        if (setup.server.isEmpty()) {
            std::fputs("split-rock-ask: --trial needs --server\n", stderr);
            return 2;
        }
        return trial(asking, QUrl(setup.server));
    }

    model.survey();
    QDBusConnection bus = QDBusConnection::sessionBus();
    ask::AskService service(&asking, &model, setup.codex,
                            beside(QStringLiteral("split-rock"), QStringLiteral(SPLIT_ROCK_INSTALLED_PROGRAM)));
    if (!bus.isConnected() || !service.publish(bus)
        || !bus.registerService(QLatin1String(ask::AskService::Service))) {
        // Another Ask already answers, or there is no bus to answer on.
        return 0;
    }

    // Ends once nothing has been asked or downloaded for a while.
    QTimer idle;
    idle.setSingleShot(true);
    idle.setInterval(std::max(1, parser.value(idleOption).toInt()) * 60 * 1000);
    const auto restart = [&idle] { idle.start(); };
    QObject::connect(&asking, &ask::Ask::said, &idle, restart);
    QObject::connect(&asking, &ask::Ask::answered, &idle, restart);
    QObject::connect(&asking, &ask::Ask::modelChanged, &idle, restart);
    QObject::connect(&idle, &QTimer::timeout, &app, [&] {
        const QString state = model.state();
        if (asking.busy() || state == QLatin1String("downloading") || state == QLatin1String("verifying")) {
            idle.start();
            return;
        }
        app.quit();
    });
    idle.start();
    return app.exec();
}
