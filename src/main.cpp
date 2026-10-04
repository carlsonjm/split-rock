// SPDX-License-Identifier: GPL-2.0-or-later
//
// Milestone 0's test program: Genie's window, talking to real assistants over
// the Agent Client Protocol.
//
//   split-rock-m0 [--assistants DIR] [--state DIR] [--pretend SCRIPT] [--guide DIR]...
//
// --assistants  where the assistants' adapters were fetched
//               (default: "assistants" beside the program)
// --state       where each assistant keeps its own sign-in
//               (default: Split Rock's folder in the person's data folder)
// --pretend     also offer the pretend assistant, following SCRIPT
// --guide       also read the guide and tour under DIR, which holds guide/
//               and tour/ as a checkout does (repeatable)

#include "app/Assistants.h"
#include "app/Conversation.h"
#include "app/QmlContext.h"
#include "desktop/Shell.h"
#include "guide/GuideLibrary.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
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
    const QCommandLineOption guideOption(QStringLiteral("guide"),
                                         QStringLiteral("Also read the guide and tour under this folder."),
                                         QStringLiteral("dir"));
    parser.addOptions({assistantsOption, stateOption, pretendOption, guideOption});
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

    // The guide in the box: what is installed for every person, and any folder
    // named here, such as a checkout.
    QStringList guideRoots = splitrock::GuideLibrary::systemRoots();
    for (const QString &dir : parser.values(guideOption))
        guideRoots.append(QDir(dir).absolutePath());
    splitrock::GuideLibrary guide;
    guide.load(guideRoots);
    splitrock::GuideContext context;
    context.desktops = guide.sources();
    // What the hardware says it is: read, never written.
    context.deviceProduct = splitrock::computerName();
    conversation.setGuide(guide, context);
    QQmlApplicationEngine engine;
    splitrock::Shell shell(&engine, QDBusConnection::sessionBus());
    engine.rootContext()->setContextProperty(QStringLiteral("conversation"), &conversation);
    engine.rootContext()->setContextProperty(QStringLiteral("shell"), &shell);
    splitrock::exposeProduct(engine.rootContext());
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    auto *card = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!card)
        return 1;
    QObject::connect(&shell, &splitrock::Shell::quitRequested, &app, &QCoreApplication::quit);
    shell.open(card);
    return app.exec();
}
