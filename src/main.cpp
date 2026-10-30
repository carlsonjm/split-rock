// SPDX-License-Identifier: GPL-2.0-or-later
//
// Split Rock's Helpers: one at a time, each in a window of its own.
//
//   split-rock --card NAME [--query TEXT] [--notebook DIR]
//   split-rock --codex [--question TEXT]
//   split-rock --settings
//   split-rock --health-report
//   split-rock --health-facts
//   split-rock --set-shortcut COMPONENT ACTION KEYS NAME
//
// --card opens one Helper, such as space, in an ordinary window: Core and
// Search open Free up space and the others this way (docs/CORE.md). Split Rock
// runs once: a second start hands its arguments to the one running and ends.
// It ends once the last Helper's window has closed.
//
// --codex opens Ask Codex, the window for the bigger questions Ask hands on
// (docs/ASK.md, Codex), with the question; with none, it only signs in, as
// Settings asks.
//
// --settings opens Split Rock Settings, Ask's own page, for a computer with or
// without Shuffle (docs/ASK.md, Split Rock Settings).
//
// --notebook    the notebook folder (default: Notebook in the Documents folder)
//
// --health-report looks the computer over as Health check does and prints what
// it finds, with no window, no assistant and no language model. It changes
// nothing, not even the last look Health check keeps. It ends with 1 only when
// the look could not run to its end. --health-facts is the same look for
// Core, one line per item: its state (good, look or problem), id, title and
// detail, separated by tabs.
//
// --set-shortcut sets one of the desktop's keyboard shortcuts, as What changed
// puts one back: COMPONENT and ACTION as the shortcuts file names them, KEYS
// as it writes them, several apart by tabs, or empty for none. The running
// desktop holds the shortcuts and writes their file itself, so it is asked on
// the bus. It ends with 1 when the desktop didn't take it.

#include "app/ChangeRunner.h"
#include "app/HealthCheck.h"
#include "app/Helpers.h"
#include "app/Notebook.h"
#include "app/QmlContext.h"
#include "codex/Chat.h"
#include "settings/AskSettings.h"
#include "desktop/Entry.h"
#include "tools/Capabilities.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QKeySequence>
#include <QDBusReply>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QProcess>
#include <QQuickWindow>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace {

const QCommandLineOption queryOption(QStringLiteral("query"),
                                     QStringLiteral("With --card install: what to look for, or to remove."),
                                     QStringLiteral("text"));
const QCommandLineOption cardOption(QStringLiteral("card"),
                                    QStringLiteral("Open one Helper: update, space, install, uninstall, apps, health, "
                                                   "changes, restore or community."),
                                    QStringLiteral("name"));
const QCommandLineOption codexOption(QStringLiteral("codex"),
                                     QStringLiteral("Open Ask Codex, with --question, or only to sign in."));
const QCommandLineOption questionOption(QStringLiteral("question"),
                                        QStringLiteral("With --codex: the question Codex takes."),
                                        QStringLiteral("text"));
const QCommandLineOption settingsOption(QStringLiteral("settings"), QStringLiteral("Open Split Rock Settings."));
const QCommandLineOption notebookOption(QStringLiteral("notebook"),
                                        QStringLiteral("The notebook folder (default: Notebook in the Documents folder)."),
                                        QStringLiteral("dir"));

// The session ends Split Rock with a signal. It quits the usual way instead, so
// a Helper's window closes before what it shows is gone.
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

// The look runs as the person, the same script as the card's, and waits for it.
// facts: one line per item for Core, its state, id, title and detail between
// tabs, instead of the report for a person.
int healthReport(int argc, char **argv, bool facts)
{
    QCoreApplication app(argc, argv);
    QProcess look;
    look.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), splitrock::HealthCheck::defaultLookScript()});
    if (!look.waitForFinished(60000)) {
        look.kill();
        std::fputs("Health check: the look didn't finish.\n", stderr);
        return 1;
    }
    const QString output = QString::fromUtf8(look.readAllStandardOutput());
    if (!splitrock::HealthCheck::lookFinished(output)) {
        std::fputs("Health check: the look stopped before its end.\n", stderr);
        return 1;
    }
    const auto items = splitrock::HealthCheck::readLook(output + splitrock::HealthCheck::batteryLook());
    const QString printed = facts ? splitrock::HealthCheck::facts(items) : splitrock::HealthCheck::report(items);
    std::fputs(printed.toUtf8().constData(), stdout);
    return 0;
}

// One shortcut's keys as KDE's shortcuts service takes them on the bus: up
// to four key presses, each a key with its modifiers.
struct Keys {
    QList<int> presses;
};

QDBusArgument &operator<<(QDBusArgument &argument, const Keys &keys)
{
    argument.beginStructure();
    argument << keys.presses;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, Keys &keys)
{
    argument.beginStructure();
    argument >> keys.presses;
    argument.endStructure();
    return argument;
}

} // namespace

Q_DECLARE_METATYPE(Keys)

namespace {

int setShortcut(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QStringList arguments = app.arguments();
    const qsizetype at = arguments.indexOf(QStringLiteral("--set-shortcut"));
    if (at < 0 || arguments.size() < at + 4) {
        std::fputs("split-rock --set-shortcut COMPONENT ACTION KEYS [NAME]\n", stderr);
        return 2;
    }
    const QString component = arguments.at(at + 1);
    const QString action = arguments.at(at + 2);
    const QString name = arguments.value(at + 4, action);
    QList<Keys> keys;
    for (const QString &each : arguments.at(at + 3).split(QLatin1Char('\t'), Qt::SkipEmptyParts)) {
        const QKeySequence sequence = QKeySequence::fromString(each, QKeySequence::PortableText);
        if (sequence.isEmpty()) {
            std::fprintf(stderr, "Split Rock: \"%s\" is not a shortcut.\n", qPrintable(each));
            return 1;
        }
        Keys pressed;
        for (int i = 0; i < sequence.count(); ++i)
            pressed.presses.append(sequence[i].toCombined());
        keys.append(pressed);
    }
    qDBusRegisterMetaType<Keys>();
    qDBusRegisterMetaType<QList<Keys>>();

    // The action as the service knows it: its component and its own name,
    // then the names a person reads.
    const QStringList actionId{component, action, component, name};
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"),
                                                       QStringLiteral("org.kde.KGlobalAccel"),
                                                       QStringLiteral("setForeignShortcutKeys"));
    call.setArguments({actionId, QVariant::fromValue(keys)});
    QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    if (reply.type() == QDBusMessage::ErrorMessage && reply.errorName() == QLatin1String("org.freedesktop.DBus.Error.UnknownMethod")) {
        // An older service takes one key press each.
        QList<int> presses;
        for (const Keys &k : std::as_const(keys))
            presses.append(k.presses.value(0));
        call = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/kglobalaccel"),
                                              QStringLiteral("org.kde.KGlobalAccel"), QStringLiteral("setForeignShortcut"));
        call.setArguments({actionId, QVariant::fromValue(presses)});
        reply = QDBusConnection::sessionBus().call(call, QDBus::Block, 5000);
    }
    if (reply.type() == QDBusMessage::ErrorMessage) {
        std::fprintf(stderr, "Split Rock: the desktop didn't take the shortcut: %s\n", qPrintable(reply.errorMessage()));
        return 1;
    }
    return 0;
}

// Keeps a conversation with Codex as a note: in Notes, in its Ask folder,
// where Gooseberry is installed, and otherwise in the notebook. note is
// Notes' id for it, when Notes kept it. The note is stuck to no window: the
// Codex window it was written on closes, and the note outlives it.
bool keepNote(splitrock::Notebook &notebook, const QString &title, const QString &text, QString *note, QString *error)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString service = QStringLiteral("io.github.carlsonjm.gooseberry");
    const auto call = [&bus, &service](const QString &method, const QVariantList &arguments) {
        QDBusMessage request = QDBusMessage::createMethodCall(service, QStringLiteral("/QuickNote"),
                                                              QStringLiteral("io.github.carlsonjm.Gooseberry.QuickNote"), method);
        request.setArguments(arguments);
        return bus.call(request, QDBus::Block, 5000);
    };
    const bool notes = bus.isConnected()
        && (bus.interface()->isServiceRegistered(service).value()
            || bus.interface()->activatableServiceNames().value().contains(service));
    if (notes) {
        const QDBusMessage state = call(QStringLiteral("State"), {});
        const bool busy = state.type() != QDBusMessage::ReplyMessage
            || qdbus_cast<QVariantMap>(state.arguments().value(0)).value(QStringLiteral("open")).toBool();
        // A quick note Search has open is never written over; the note
        // opens on Gooseberry's own card instead, for the person to keep.
        const QString words = title.isEmpty() ? text : title + QStringLiteral("\n\n") + text;
        if (busy)
            return call(QStringLiteral("OpenOnCard"), {QVariantMap{{QStringLiteral("text"), words}}}).type()
                == QDBusMessage::ReplyMessage;
        for (const auto &[method, arguments] : std::initializer_list<std::pair<QString, QVariantList>>{
                 {QStringLiteral("Start"), {}},
                 {QStringLiteral("SetStuck"), {QString(), QString()}},
                 {QStringLiteral("SetText"), {words}},
                 {QStringLiteral("SetFolder"), {QStringLiteral("Ask")}},
                 {QStringLiteral("Done"), {}}}) {
            const QDBusMessage reply = call(method, arguments);
            if (reply.type() != QDBusMessage::ReplyMessage) {
                if (error)
                    *error = QStringLiteral("Notes couldn't keep it.");
                return false;
            }
            const QString id = qdbus_cast<QVariantMap>(reply.arguments().value(0)).value(QStringLiteral("id")).toString();
            if (note && !id.isEmpty())
                *note = id;
        }
        return true;
    }
    return !notebook.keep(title, text, error).isEmpty();
}

// The broker's apps' tools (docs/BROKER.md § Tools), where the broker is on
// the computer.
const QString BrokerService = QStringLiteral("io.github.carlsonjm.splitrock.Broker");

QDBusMessage brokerCall(const QString &method)
{
    return QDBusMessage::createMethodCall(BrokerService, QStringLiteral("/Broker"),
                                          QStringLiteral("io.github.carlsonjm.SplitRock.Broker"), method);
}

QVariantList brokerTools()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()
        || !(bus.interface()->isServiceRegistered(BrokerService).value()
             || bus.interface()->activatableServiceNames().value().contains(BrokerService)))
        return {};
    const QDBusMessage reply = bus.call(brokerCall(QStringLiteral("Tools")), QDBus::Block, 5000);
    return reply.type() == QDBusMessage::ReplyMessage ? splitrock::tools::fromBus(reply.arguments().value(0)).toList() : QVariantList();
}

void askBroker(const QString &method, const QVariantList &arguments, const std::function<void(const QVariantMap &)> &reply)
{
    QDBusMessage request = brokerCall(method);
    request.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(request, 15000));
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [reply](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusMessage answer = watcher->reply();
        if (answer.type() != QDBusMessage::ReplyMessage)
            reply({{QStringLiteral("ok"), false}, {QStringLiteral("problem"), QStringLiteral("The broker didn't answer.")}});
        else
            reply(splitrock::tools::fromBus(answer.arguments().value(0)).toMap());
    });
}

} // namespace

int main(int argc, char **argv)
{
    // Before the desktop's own start, so it runs where there is no desktop.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--health-report") == 0 || std::strcmp(argv[i], "--health-facts") == 0)
            return healthReport(argc, argv, std::strcmp(argv[i], "--health-facts") == 0);
        if (std::strcmp(argv[i], "--set-shortcut") == 0)
            return setShortcut(argc, argv);
    }

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("split-rock"));
    splitrock::setInsideShuffle(splitrock::shuffleInstalled());
    // On its own every window's title ends with Split Rock. Inside Shuffle a
    // Helper's window is titled by the Helper alone, so the title bar centres
    // "Free up space" rather than "Free up space — Shuffle".
    if (!splitrock::insideShuffle())
        QGuiApplication::setApplicationDisplayName(splitrock::productTitle());
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION));
    // Every window's application id, and the desktop file's.
    QGuiApplication::setDesktopFileName(QLatin1String(splitrock::AppId));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Split Rock's Helpers, one at a time."));
    parser.addHelpOption();
    parser.addOptions({cardOption, queryOption, codexOption, questionOption, settingsOption, notebookOption});
    parser.process(app);
    if (!parser.isSet(cardOption) && !parser.isSet(codexOption) && !parser.isSet(settingsOption)) {
        qWarning("Split Rock opens one Helper at a time: split-rock --card NAME. See --help.");
        return 2;
    }

    // The one running Split Rock answers instead.
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString serviceName = QLatin1String(splitrock::Entry::Service);
    const auto forward = [&bus, &serviceName, &app] {
        QDBusMessage request = QDBusMessage::createMethodCall(serviceName, QLatin1String(splitrock::Entry::Path),
                                                              QLatin1String(splitrock::Entry::Interface),
                                                              QStringLiteral("CommandLine"));
        request.setArguments({app.arguments()});
        return bus.call(request, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage ? 0 : 1;
    };
    if (bus.isConnected() && bus.interface()->isServiceRegistered(serviceName))
        return forward();

    // The notebook: made, with what Split Rock writes at setup, where missing.
    // Each Helper writes what it changes there first.
    splitrock::Notebook notebook(parser.isSet(notebookOption) ? parser.value(notebookOption)
                                                              : splitrock::Notebook::defaultFolder());
    QString notebookProblem;
    if (!notebook.prepare(splitrock::DeviceFacts::read(), &notebookProblem))
        qWarning().noquote() << notebookProblem;

    splitrock::Helpers helpers;
    helpers.setNotebook(&notebook);

    splitrock::codex::Chat chat(splitrock::codex::codex(), helpers.changeRunner(), &notebook, {});

    splitrock::AskSettings askSettings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                       + QStringLiteral("/split-rock-corerc"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("askSettings"), &askSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("helpers"), &helpers);
    engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
    splitrock::exposeProduct(engine.rootContext());
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/ActionWindow.qml")));
    auto *window = qobject_cast<QQuickWindow *>(component.create(engine.rootContext()));
    QQmlComponent codexComponent(&engine, QUrl(QStringLiteral("qrc:/qml/CodexWindow.qml")));
    auto *codexWindow = qobject_cast<QQuickWindow *>(codexComponent.create(engine.rootContext()));
    QQmlComponent settingsComponent(&engine, QUrl(QStringLiteral("qrc:/qml/SettingsWindow.qml")));
    auto *settingsWindow = qobject_cast<QQuickWindow *>(settingsComponent.create(engine.rootContext()));
    if (!window || !codexWindow || !settingsWindow) {
        qWarning().noquote() << component.errorString() << codexComponent.errorString()
                             << settingsComponent.errorString();
        return 1;
    }
    const auto showWindow = [](QQuickWindow *shown) {
        shown->show();
        shown->raise();
        shown->requestActivate();
    };
    // What Codex hands on: a Helper opens in its own window, and Keep keeps
    // the conversation as a note.
    chat.setHands({[&](const QString &card, const QString &query) {
                       if (!helpers.openChore(card, query))
                           return false;
                       showWindow(window);
                       return true;
                   },
                   [&notebook](const QString &title, const QString &text, QString *note, QString *error) {
                       return keepNote(notebook, title, text, note, error);
                   },
                   // Notes, on the Ask folder, at the note.
                   [](const QString &note) {
                       QDBusMessage request = QDBusMessage::createMethodCall(
                           QStringLiteral("io.github.carlsonjm.gooseberry"), QStringLiteral("/QuickNote"),
                           QStringLiteral("io.github.carlsonjm.Gooseberry.QuickNote"), QStringLiteral("OpenBoard"));
                       request.setArguments({note, QString()});
                       const QDBusMessage reply = QDBusConnection::sessionBus().call(request, QDBus::Block, 5000);
                       return reply.type() == QDBusMessage::ReplyMessage && reply.arguments().value(0).toBool();
                   },
                   // The apps' tools, through the broker, with Codex as the
                   // caller.
                   [] { return brokerTools(); },
                   [](const QString &tool, const QVariantMap &arguments, const std::function<void(const QVariantMap &)> &reply) {
                       askBroker(QStringLiteral("Call"), {tool, arguments, QStringLiteral("codex")}, reply);
                   },
                   [](const QString &token, const std::function<void(const QVariantMap &)> &reply) {
                       askBroker(QStringLiteral("Run"), {token}, reply);
                   }});
    QObject::connect(&chat, &splitrock::codex::Chat::openPage, &app,
                     [](const QString &page) { QDesktopServices::openUrl(QUrl(page)); });
    // Split Rock ends once its windows have closed, after a moment's grace:
    // one Helper handing over to another closes the window and opens it
    // again. The session bus's next start brings it back.
    const auto quitWhenDone = [&] {
        QTimer::singleShot(std::chrono::seconds(2), &app, [&] {
            if (!window->isVisible() && !codexWindow->isVisible() && !settingsWindow->isVisible() && !helpers.choreBusy()
                && !(helpers.changeRunner() && helpers.changeRunner()->busy()))
                QCoreApplication::quit();
        });
    };
    for (QQuickWindow *each : {window, codexWindow, settingsWindow}) {
        QObject::connect(each, &QWindow::visibleChanged, &app, [quitWhenDone](bool visible) {
            if (!visible)
                quitWhenDone();
        });
    }

    const auto handle = [&](const QStringList &arguments) {
        QCommandLineParser request;
        request.addOptions({cardOption, queryOption, codexOption, questionOption, settingsOption, notebookOption});
        request.parse(arguments);
        if (request.isSet(codexOption)) {
            chat.begin(request.value(questionOption));
            showWindow(codexWindow);
            return;
        }
        if (request.isSet(settingsOption)) {
            showWindow(settingsWindow);
            return;
        }
        if (!request.isSet(cardOption) || !helpers.openChore(request.value(cardOption), request.value(queryOption)))
            return;
        showWindow(window);
    };

    splitrock::Entry entry(handle);
    if (bus.isConnected() && bus.registerObject(QLatin1String(splitrock::Entry::Path), &entry, QDBusConnection::ExportScriptableSlots)) {
        const auto claimed = bus.interface()->registerService(serviceName, QDBusConnectionInterface::DontQueueService,
                                                              QDBusConnectionInterface::DontAllowReplacement);
        if (claimed.isValid() && claimed.value() == QDBusConnectionInterface::ServiceNotRegistered)
            return forward(); // Another started at the same moment.
    }
    app.setQuitOnLastWindowClosed(false);
    quitOnSignals(app);

    handle(app.arguments());
    if (!window->isVisible() && !codexWindow->isVisible() && !settingsWindow->isVisible())
        return 1; // Nothing opened: an unknown Helper, or one already busy.
    const int code = app.exec();
    // The windows go first, while everything they show is still there.
    delete settingsWindow;
    delete codexWindow;
    delete window;
    return code;
}
