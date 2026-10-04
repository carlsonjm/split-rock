// SPDX-License-Identifier: GPL-2.0-or-later
//
// Milestone 0's test program: the Just Type sheet, talking to real assistants
// over the Agent Client Protocol.
//
//   split-rock-m0 [--assistants DIR] [--state DIR] [--pretend SCRIPT]
//
// --assistants  where the assistants' adapters were fetched
//               (default: "assistants" beside the program)
// --state       where each assistant keeps its own sign-in
//               (default: Split Rock's folder in the person's data folder)
// --pretend     also offer the pretend assistant, following SCRIPT

#include "app/Assistants.h"
#include "app/Conversation.h"
#include "app/QmlContext.h"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("split-rock"));
    // Genie inside Shuffle, Split Rock on any other Plasma desktop.
    splitrock::setInsideShuffle(splitrock::shuffleInstalled());
    QGuiApplication::setApplicationDisplayName(splitrock::productTitle());
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("split-rock"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Split Rock, Milestone 0: the conversation works."));
    parser.addHelpOption();
    const QCommandLineOption assistantsOption(QStringLiteral("assistants"),
                                              QStringLiteral("Where the assistants' adapters were fetched."),
                                              QStringLiteral("dir"));
    const QCommandLineOption stateOption(QStringLiteral("state"),
                                         QStringLiteral("Where each assistant keeps its own sign-in."),
                                         QStringLiteral("dir"));
    const QCommandLineOption pretendOption(QStringLiteral("pretend"),
                                           QStringLiteral("Also offer the pretend assistant, following this script."),
                                           QStringLiteral("script"));
    parser.addOptions({assistantsOption, stateOption, pretendOption});
    parser.process(app);

    const QDir programDir(QCoreApplication::applicationDirPath());
    const QString adapters = parser.isSet(assistantsOption)
        ? QDir(parser.value(assistantsOption)).absolutePath()
        : programDir.filePath(QStringLiteral("assistants"));
    const QString state = parser.isSet(stateOption)
        ? QDir(parser.value(stateOption)).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/assistants");

    QList<splitrock::Assistant> assistants = splitrock::realAssistants(adapters, state);
    if (parser.isSet(pretendOption)) {
        assistants.append(splitrock::pretendAssistant(programDir.filePath(QStringLiteral("split-rock-pretend-assistant")),
                                                      QDir(parser.value(pretendOption)).absolutePath(), state));
    }

    splitrock::Conversation conversation(assistants);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("conversation"), &conversation);
    splitrock::exposeProduct(engine.rootContext());
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
