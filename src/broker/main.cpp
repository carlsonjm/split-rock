// SPDX-License-Identifier: GPL-2.0-or-later
//
// split-rock-broker: the models, for the Good Input apps that ask
// (docs/BROKER.md). The session bus starts it on the first call, and it ends
// on its own once nothing has been asked for a while, which gives the model's
// memory back. A second start ends at once.

#include "ask/LocalModel.h"
#include "broker/Broker.h"
#include "broker/BrokerService.h"
#include "broker/Toolbox.h"
#include "tools/Capabilities.h"
#include "codex/Agent.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

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

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("split-rock-broker"));
    app.setApplicationVersion(QStringLiteral(PROJECT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("The models, for the Good Input apps that ask."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption modelsOption(QStringLiteral("models"), QStringLiteral("Where the model is kept."),
                                          QStringLiteral("DIR"));
    const QCommandLineOption serverOption(QStringLiteral("server"), QStringLiteral("A llama-server already running."),
                                          QStringLiteral("URL"));
    const QCommandLineOption idleOption(QStringLiteral("idle"),
                                        QStringLiteral("Minutes with nothing asked before it ends."),
                                        QStringLiteral("MINUTES"), QStringLiteral("10"));
    parser.addOptions({modelsOption, serverOption, idleOption});
    parser.process(app);

    // The model Ask downloads: one copy on the computer, used by both.
    const QString models = parser.isSet(modelsOption)
        ? QDir(parser.value(modelsOption)).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/split-rock/models");
    const QString engine = findEngine();
    LocalModel model(engine, beside(QStringLiteral("models.json"), QStringLiteral(SPLIT_ROCK_INSTALLED_MODELS)), models);
    model.survey();

    broker::Broker::Setup setup;
    setup.engine = engine;
    setup.server = parser.value(serverOption);
    setup.codex = codex::codex();
    broker::Broker broker(&model, setup);

    QDBusConnection bus = QDBusConnection::sessionBus();
    // The apps' tools, from the files they install beside their bus services.
    broker::Toolbox toolbox(tools::Capabilities::defaultFolders(), bus);
    broker::BrokerService service(&broker, &toolbox);
    if (!bus.isConnected() || !service.publish(bus)
        || !bus.registerService(QLatin1String(broker::BrokerService::Service))) {
        // Another broker already answers, or there is no bus to answer on.
        return 0;
    }

    QTimer idle;
    idle.setSingleShot(true);
    idle.setInterval(std::max(1, parser.value(idleOption).toInt()) * 60 * 1000);
    const auto restart = [&idle] { idle.start(); };
    QObject::connect(&broker, &broker::Broker::sorted, &idle, restart);
    QObject::connect(&broker, &broker::Broker::said, &idle, restart);
    QObject::connect(&broker, &broker::Broker::drafted, &idle, restart);
    QObject::connect(&broker, &broker::Broker::answered, &idle, restart);
    QObject::connect(&broker, &broker::Broker::failed, &idle, restart);
    QObject::connect(&toolbox, &broker::Toolbox::used, &idle, restart);
    QObject::connect(&idle, &QTimer::timeout, &app, [&] {
        if (broker.busy()) {
            idle.start();
            return;
        }
        app.quit();
    });
    idle.start();
    return app.exec();
}
