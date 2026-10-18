// SPDX-License-Identifier: GPL-2.0-or-later
//
// Split Rock's Helpers: one at a time, each in a window of its own.
//
//   split-rock --card NAME [--query TEXT] [--notebook DIR]
//   split-rock --health-report
//
// --card opens one Helper, such as space, in an ordinary window: Core and
// Search open Free up space and the others this way (docs/CORE.md). Split Rock
// runs once: a second start hands its arguments to the one running and ends.
// It ends once the last Helper's window has closed.
//
// --notebook    the notebook folder (default: Notebook in the Documents folder)
//
// --health-report looks the computer over as Health check does and prints what
// it finds, with no window, no assistant and no language model. It changes
// nothing, not even the last look Health check keeps. It ends with 1 only when
// the look could not run to its end.

#include "app/HealthCheck.h"
#include "app/Helpers.h"
#include "app/Notebook.h"
#include "app/QmlContext.h"
#include "desktop/Entry.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QProcess>
#include <QQuickWindow>
#include <QSocketNotifier>
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
                                    QStringLiteral("Open one Helper: update, space, install, uninstall, health, changes, "
                                                   "restore or community."),
                                    QStringLiteral("name"));
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
int healthReport(int argc, char **argv)
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
    std::fputs(splitrock::HealthCheck::report(splitrock::HealthCheck::readLook(output)).toUtf8().constData(), stdout);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    // Before the desktop's own start, so it runs where there is no desktop.
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--health-report") == 0)
            return healthReport(argc, argv);

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
    parser.addOptions({cardOption, queryOption, notebookOption});
    parser.process(app);
    if (!parser.isSet(cardOption)) {
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

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("helpers"), &helpers);
    splitrock::exposeProduct(engine.rootContext());
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/ActionWindow.qml")));
    auto *window = qobject_cast<QQuickWindow *>(component.create(engine.rootContext()));
    if (!window) {
        qWarning().noquote() << component.errorString();
        return 1;
    }
    // Split Rock ends once the Helper's window has closed, after a moment's
    // grace: one Helper handing over to another closes the window and opens it
    // again. The session bus's next start brings it back.
    QObject::connect(window, &QWindow::visibleChanged, &app, [&](bool visible) {
        if (!visible)
            QTimer::singleShot(std::chrono::seconds(2), &app, [&] {
                if (!window->isVisible() && !helpers.choreBusy())
                    QCoreApplication::quit();
            });
    });

    const auto handle = [&](const QStringList &arguments) {
        QCommandLineParser request;
        request.addOptions({cardOption, queryOption, notebookOption});
        request.parse(arguments);
        if (!request.isSet(cardOption) || !helpers.openChore(request.value(cardOption), request.value(queryOption)))
            return;
        window->show();
        window->raise();
        window->requestActivate();
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
    if (!window->isVisible())
        return 1; // Nothing opened: an unknown Helper, or one already busy.
    const int code = app.exec();
    // The window goes first, while everything it shows is still there.
    delete window;
    return code;
}
