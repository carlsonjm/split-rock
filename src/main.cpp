// SPDX-License-Identifier: GPL-2.0-or-later
//
// Milestone 0's program: Split Rock, talking to real assistants over the Agent
// Client Protocol, and offering its conversation to Search on the session bus.
//
//   split-rock [--ask | --background] [--assistants DIR] [--state DIR] [--pretend SCRIPT] [--guide DIR]...
//
// With no arguments, as from its entry, it opens the full window. --ask opens
// the card in Search's shape; --background opens nothing, as when Search's
// first call starts it. Split Rock runs once: a second start hands its
// arguments to the one running and ends.
//
// --assistants  where the assistants' adapters were fetched
//               (default: "assistants" beside the program, or where the
//               developer install put them)
// --state       where each assistant keeps its own sign-in
//               (default: Split Rock's folder in the person's data folder)
// --notebook    the notebook folder (default: Notebook in the Documents folder)
// --pretend     also offer the pretend assistant, following SCRIPT
// --guide       also read the guide and tour under DIR, which holds guide/
//               and tour/ as a checkout does (repeatable)

#include "app/Assistants.h"
#include "app/Conversation.h"
#include "app/Notebook.h"
#include "app/QmlContext.h"
#include "desktop/ConversationService.h"
#include "desktop/Entry.h"
#include "desktop/Shell.h"
#include "guide/GuideLibrary.h"

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {

const QCommandLineOption askOption(QStringLiteral("ask"), QStringLiteral("Open the card in Search's shape."));
const QCommandLineOption backgroundOption(QStringLiteral("background"),
                                          QStringLiteral("Open nothing: the conversation waits for Search."));

// The session ends Split Rock with a signal. It quits the usual way instead, so
// the assistant is stopped and its record of the conversation removed.
int signalPipe[2] = {-1, -1};

void onSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] const auto written = ::write(signalPipe[0], &byte, 1);
}

void quitOnSignals(QGuiApplication &app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signalPipe) != 0)
        return;
    auto *notifier = new QSocketNotifier(signalPipe[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, &QCoreApplication::quit);
    for (int signal : {SIGTERM, SIGINT, SIGHUP})
        std::signal(signal, onSignal);
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("split-rock"));
    // Genie inside Shuffle, Split Rock on any other Plasma desktop.
    splitrock::setInsideShuffle(splitrock::shuffleInstalled());
    QGuiApplication::setApplicationDisplayName(splitrock::productTitle());
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    // Every window's application id, and the desktop file's.
    QGuiApplication::setDesktopFileName(QLatin1String(splitrock::AppId));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Split Rock, Milestone 0: the conversation works."));
    parser.addHelpOption();
    const QCommandLineOption assistantsOption(QStringLiteral("assistants"),
                                              QStringLiteral("Where the assistants' adapters were fetched."),
                                              QStringLiteral("dir"));
    const QCommandLineOption stateOption(QStringLiteral("state"),
                                         QStringLiteral("Where each assistant keeps its own sign-in."),
                                         QStringLiteral("dir"));
    const QCommandLineOption notebookOption(QStringLiteral("notebook"),
                                            QStringLiteral("The notebook folder (default: Notebook in the Documents folder)."),
                                            QStringLiteral("dir"));
    const QCommandLineOption pretendOption(QStringLiteral("pretend"),
                                           QStringLiteral("Also offer the pretend assistant, following this script."),
                                           QStringLiteral("script"));
    const QCommandLineOption guideOption(QStringLiteral("guide"),
                                         QStringLiteral("Also read the guide and tour under this folder."),
                                         QStringLiteral("dir"));
    parser.addOptions({askOption, backgroundOption, assistantsOption, stateOption, notebookOption, pretendOption, guideOption});
    parser.process(app);

    // The one running Split Rock answers instead.
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString serviceName = QLatin1String(splitrock::ConversationService::Service);
    const auto forward = [&bus, &serviceName, &app] {
        QDBusMessage request = QDBusMessage::createMethodCall(serviceName, QLatin1String(splitrock::Entry::Path),
                                                              QLatin1String(splitrock::Entry::Interface),
                                                              QStringLiteral("CommandLine"));
        request.setArguments({app.arguments()});
        return bus.call(request, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage ? 0 : 1;
    };
    if (bus.isConnected() && bus.interface()->isServiceRegistered(serviceName))
        return forward();

    const QDir programDir(QCoreApplication::applicationDirPath());
    // Adapters beside the program in a build; installed, where install.sh put
    // them.
    const QString adapters = parser.isSet(assistantsOption) ? QDir(parser.value(assistantsOption)).absolutePath()
        : programDir.exists(QStringLiteral("assistants"))   ? programDir.filePath(QStringLiteral("assistants"))
                                                            : QStringLiteral(SPLIT_ROCK_INSTALLED_ADAPTERS);
    const QString state = parser.isSet(stateOption)
        ? QDir(parser.value(stateOption)).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/assistants");

    QList<splitrock::Assistant> assistants = splitrock::realAssistants(adapters, state);
    if (parser.isSet(pretendOption)) {
        assistants.append(splitrock::pretendAssistant(programDir.filePath(QStringLiteral("split-rock-pretend-assistant")),
                                                      QDir(parser.value(pretendOption)).absolutePath(), state));
    }

    // The notebook: made, with what Split Rock writes at setup, where missing.
    splitrock::Notebook notebook(parser.isSet(notebookOption) ? parser.value(notebookOption)
                                                              : splitrock::Notebook::defaultFolder());
    QString notebookProblem;
    if (!notebook.prepare(splitrock::DeviceFacts::read(), &notebookProblem))
        qWarning().noquote() << notebookProblem;

    splitrock::Conversation conversation(assistants);
    conversation.setNotebook(&notebook);

    // The guide in the box: what is installed for every person, and any folder
    // named here, such as a checkout.
    QStringList guideRoots = splitrock::GuideLibrary::systemRoots();
    for (const QString &dir : parser.values(guideOption))
        guideRoots.append(QDir(dir).absolutePath());
    splitrock::GuideLibrary guide;
    // The device's own entries, learned on this computer, sit in the notebook.
    guide.load(guideRoots, QDir(notebook.folder()).filePath(QStringLiteral("Device")));
    splitrock::GuideContext context;
    context.desktops = guide.sources();
    // What the hardware says it is: read, never written.
    context.deviceProduct = splitrock::computerName();
    conversation.setGuide(guide, context);
    QQmlApplicationEngine engine;
    splitrock::Shell shell(&engine, bus);
    engine.rootContext()->setContextProperty(QStringLiteral("conversation"), &conversation);
    engine.rootContext()->setContextProperty(QStringLiteral("shell"), &shell);
    splitrock::exposeProduct(engine.rootContext());
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    auto *card = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!card)
        return 1;

    const auto handle = [&](const QStringList &arguments) {
        QCommandLineParser request;
        request.addOptions({askOption, backgroundOption, assistantsOption, stateOption, notebookOption, pretendOption, guideOption});
        request.parse(arguments);
        if (request.isSet(backgroundOption))
            return;
        // The assistant the person chose last time starts with the window.
        conversation.resumeChosen();
        if (request.isSet(askOption))
            shell.open(card);
        else
            shell.openFull(QString());
    };

    // The conversation for Search, held while Split Rock runs.
    splitrock::ConversationService service(&conversation, &shell);
    splitrock::Entry entry(handle);
    bool serving = false;
    if (bus.isConnected() && service.publish(bus)
        && bus.registerObject(QLatin1String(splitrock::Entry::Path), &entry, QDBusConnection::ExportScriptableSlots)) {
        const auto claimed = bus.interface()->registerService(serviceName, QDBusConnectionInterface::DontQueueService,
                                                              QDBusConnectionInterface::DontAllowReplacement);
        if (claimed.isValid() && claimed.value() == QDBusConnectionInterface::ServiceNotRegistered)
            return forward(); // Another started at the same moment.
        serving = claimed.isValid() && claimed.value() == QDBusConnectionInterface::ServiceRegistered;
    }

    if (serving) {
        // Split Rock keeps the conversation for Search after its windows
        // close; the session ends it.
        app.setQuitOnLastWindowClosed(false);
    } else {
        if (parser.isSet(backgroundOption)) {
            qWarning("Split Rock could not offer its conversation on the session bus, so there is nothing to wait for.");
            return 1;
        }
        QObject::connect(&shell, &splitrock::Shell::quitRequested, &app, &QCoreApplication::quit);
    }
    quitOnSignals(app);

    handle(app.arguments());
    const int code = app.exec();
    // The windows go first, while everything they show is still there.
    qDeleteAll(engine.rootObjects());
    return code;
}
