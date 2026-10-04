// SPDX-License-Identifier: GPL-2.0-or-later
#include "desktop/Shell.h"

#include "desktop/Kadunce.h"

#include <QCursor>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <QUuid>

#include <functional>

#ifdef SPLIT_ROCK_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

namespace splitrock {

namespace {

constexpr char CompanionInterface[] = "io.github.carlsonjm.SplitRock.Companion";
// How long the card takes to grow, as the QML draws it, and a little more.
constexpr int GrowMs = 240;
// How long the card waits for the full window before going anyway.
constexpr int HandOverLimitMs = 10000;

using Reply = std::function<void(const QDBusMessage &)>;

// Every call to Kadunce is answered later, so the card never waits on the
// compositor; a call that fails is answered with the error.
void ask(QDBusConnection &bus, QObject *context, const QString &method, const QVariantList &arguments, int timeout,
         Reply done)
{
    QDBusMessage request = QDBusMessage::createMethodCall(QLatin1String(kadunce::Service), QLatin1String(kadunce::Path),
                                                          QLatin1String(kadunce::Interface), method);
    request.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(request, timeout), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [watcher, done = std::move(done)] {
        watcher->deleteLater();
        done(watcher->reply());
    });
}

bool answered(const QDBusMessage &reply)
{
    return reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty();
}

} // namespace

Shell::Shell(QQmlApplicationEngine *engine, QDBusConnection bus, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_bus(std::move(bus))
{
    if (m_bus.isConnected())
        m_bus.registerObject(QLatin1String(ObjectPath), this, QDBusConnection::ExportScriptableSlots);
}

Shell::~Shell()
{
    // Kadunce ends the lease itself when this program's bus name goes.
    if (m_bus.isConnected())
        m_bus.unregisterObject(QLatin1String(ObjectPath));
    delete m_full;
}

QString Shell::appId() const
{
    const QString id = QGuiApplication::desktopFileName();
    return id.isEmpty() ? QStringLiteral("split-rock") : id;
}

void Shell::open(QQuickWindow *card)
{
    m_card = card;
    connect(card, SIGNAL(finished()), this, SLOT(cardFinished()));
    decide();
}

void Shell::decide()
{
    const quint64 generation = ++m_generation;
    // Whatever Kadunce says, or doesn't, the card is up within a second.
    QTimer::singleShot(1000, this, [this, generation] {
        if (generation == m_generation && !m_placed)
            placeStandalone(QStringLiteral("standalone"));
    });
    if (!m_bus.isConnected()) {
        placeStandalone(QStringLiteral("standalone"));
        return;
    }
    const auto stale = [this, generation] { return generation != m_generation || m_placed; };
    ask(m_bus, this, QStringLiteral("companionGuestProtocolVersion"), {}, 350, [this, stale](const QDBusMessage &version) {
        if (stale())
            return;
        // No Kadunce, or one that speaks another protocol: the card stands
        // alone, as on any other desktop.
        if (!answered(version) || version.arguments().first().toInt() != kadunce::CompanionProtocol) {
            placeStandalone(QStringLiteral("standalone"));
            return;
        }
        ask(m_bus, this, QStringLiteral("workspaceContext"), {}, 500, [this, stale](const QDBusMessage &context) {
            if (stale())
                return;
            const QString presentation = answered(context) ? kadunce::presentation(context.arguments().first().toString())
                                                           : QString();
            // Over an Active card there is no guest: the card draws itself.
            if (presentation == QLatin1String("active")) {
                placeStandalone(QStringLiteral("overActive"));
                return;
            }
            if (presentation != QLatin1String("cardLine")) {
                placeStandalone(QStringLiteral("standalone"));
                return;
            }
            const QVariantList arguments{m_bus.baseService(), QLatin1String(ObjectPath), QLatin1String(CompanionInterface)};
            ask(m_bus, this, QStringLiteral("beginCompanionGuest"), arguments, 700, [this, stale](const QDBusMessage &begin) {
                const kadunce::GuestPlace place =
                    kadunce::parseBegin(answered(begin) ? begin.arguments().first().toString() : QString());
                QScreen *screen = nullptr;
                for (QScreen *candidate : QGuiApplication::screens()) {
                    if (candidate->name() == place.output)
                        screen = candidate;
                }
                if (stale() || !place.accepted || !screen) {
                    // A centre given too late, or on a screen this program
                    // cannot find, is handed straight back.
                    if (place.accepted)
                        ask(m_bus, this, QStringLiteral("endLauncherGuest"), {}, 500, [](const QDBusMessage &) {});
                    if (!stale())
                        placeStandalone(QStringLiteral("standalone"));
                    return;
                }
                m_holdsGuest = true;
                placeGuest(screen, place.card, place.active);
            });
        });
    });
}

void Shell::placeStandalone(const QString &mode)
{
    m_mode = mode;
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    place(screen ? screen : QGuiApplication::primaryScreen());
}

void Shell::placeGuest(QScreen *screen, const QRect &card, const QRect &active)
{
    m_mode = QStringLiteral("guest");
    const QPoint origin = screen->geometry().topLeft();
    m_guestCard = card.translated(-origin);
    m_guestActive = active.translated(-origin);
    place(screen);
}

void Shell::place(QScreen *screen)
{
    m_placed = true;
    if (!m_card->isVisible() && screen) {
        configureSurface(screen);
        if (m_layered || !QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
            // The card's window lies across the whole screen, panels and all,
            // so Kadunce's rectangles land where it means them.
            m_card->setGeometry(screen->geometry());
            m_workArea = screen->availableGeometry().translated(-screen->geometry().topLeft());
            m_card->show();
        } else {
            // A Wayland desktop without its own surfaces for this program
            // places the window itself; maximized, it is the work area.
            m_workArea = {};
            m_card->showMaximized();
        }
        m_card->requestActivate();
    }
    setGuestMask();
    Q_EMIT placeChanged();
    QMetaObject::invokeMethod(m_card, "open");
}

void Shell::configureSurface(QScreen *screen)
{
    m_card->setScreen(screen);
#ifdef SPLIT_ROCK_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        // A surface of the desktop's own, as Search's is: Kadunce makes an
        // ordinary window a card, and a card cannot hold Spread's centre. Top,
        // not overlay: KWin keeps the on-screen keys in the overlay layer, and
        // an overlay surface mapped after them would take every touch meant
        // for them.
        auto *surface = LayerShellQt::Window::get(m_card);
        surface->setScreen(screen);
        surface->setScope(QStringLiteral("split-rock-card"));
        surface->setLayer(LayerShellQt::Window::LayerTop);
        LayerShellQt::Window::Anchors anchors;
        anchors.setFlag(LayerShellQt::Window::AnchorTop);
        anchors.setFlag(LayerShellQt::Window::AnchorBottom);
        anchors.setFlag(LayerShellQt::Window::AnchorLeft);
        anchors.setFlag(LayerShellQt::Window::AnchorRight);
        surface->setAnchors(anchors);
        surface->setDesiredSize(QSize(0, 0));
        surface->setExclusiveZone(-1);
        surface->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityExclusive);
        surface->setActivateOnShow(true);
        m_layered = true;
        return;
    }
#endif
    m_card->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
}

void Shell::setGuestMask()
{
    if (!m_card)
        return;
    // As a guest the card takes only its own touches; one beside it is
    // Kadunce's, which closes the card.
    if (m_mode == QLatin1String("guest") && m_holdsGuest)
        m_card->setMask(QRegion(m_expanded ? m_guestActive : m_guestCard.adjusted(-16, -16, 16, 16)));
    else
        m_card->setMask(QRegion());
}

void Shell::expand()
{
    if (m_expanded || m_handingOver || !m_card)
        return;
    if (m_mode == QLatin1String("guest") && m_holdsGuest) {
        ask(m_bus, this, QStringLiteral("setLauncherGuestExpanded"), {true}, 700, [this](const QDBusMessage &reply) {
            if (m_expanded)
                return;
            if (!answered(reply) || !reply.arguments().first().toBool()) {
                // Kadunce would not grow the centre: give it back, and grow
                // over the work instead.
                endGuest();
                m_mode = QStringLiteral("standalone");
                Q_EMIT placeChanged();
            }
            m_expanded = true;
            setGuestMask();
            Q_EMIT expandedChanged();
            QTimer::singleShot(GrowMs, this, &Shell::handOver);
        });
        return;
    }
    m_expanded = true;
    Q_EMIT expandedChanged();
    QTimer::singleShot(GrowMs, this, &Shell::handOver);
}

void Shell::handOver()
{
    if (m_handingOver)
        return;
    m_handingOver = true;

    if (m_mode == QLatin1String("guest") && m_holdsGuest) {
        // Kadunce holds the centre until the full window arrives, then makes
        // it the Active card in the centre's place.
        const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QVariantList arguments{QStringList{appId()}, token};
        ask(m_bus, this, QStringLiteral("prepareLauncherGuestLaunch"), arguments, 700, [this, token](const QDBusMessage &reply) {
            if (!m_holdsGuest || !answered(reply) || !reply.arguments().first().toBool()) {
                endGuest();
                showFull();
                QTimer::singleShot(300, this, &Shell::finishHandOver);
                return;
            }
            m_launchToken = token;
            showFull();
            QTimer::singleShot(HandOverLimitMs, this, [this, token] {
                if (m_launchToken != token || m_handedOver)
                    return;
                ask(m_bus, this, QStringLiteral("cancelLauncherGuestLaunch"), {}, 500, [](const QDBusMessage &) {});
                endGuest();
                finishHandOver();
            });
        });
        return;
    }

    if (m_mode == QLatin1String("overActive") && m_bus.isConnected()) {
        // The full window takes the Active card's place. The card stays up
        // until Kadunce shows that window, so nothing flashes between them.
        m_bus.connect(QLatin1String(kadunce::Service), QLatin1String(kadunce::Path), QLatin1String(kadunce::Interface),
                      QStringLiteral("workspaceContextChanged"), this, SLOT(workspaceChanged()));
        showFull();
        workspaceChanged();
        QTimer::singleShot(HandOverLimitMs, this, &Shell::finishHandOver);
        return;
    }

    showFull();
    QTimer::singleShot(300, this, &Shell::finishHandOver);
}

void Shell::workspaceChanged()
{
    if (!m_handingOver || m_handedOver)
        return;
    ask(m_bus, this, QStringLiteral("workspaceContext"), {}, 500, [this](const QDBusMessage &reply) {
        if (answered(reply) && kadunce::selectedIsApplication(reply.arguments().first().toString(), appId()))
            finishHandOver();
    });
}

void Shell::finishHandOver()
{
    if (m_handedOver)
        return;
    m_handedOver = true;
    m_launchToken.clear();
    m_bus.disconnect(QLatin1String(kadunce::Service), QLatin1String(kadunce::Path), QLatin1String(kadunce::Interface),
                     QStringLiteral("workspaceContextChanged"), this, SLOT(workspaceChanged()));
    Q_EMIT handedOver();
    if (m_card)
        QMetaObject::invokeMethod(m_card, "finish");
}

void Shell::showFull()
{
    if (!m_full) {
        QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/qml/FullWindow.qml")));
        QObject *object = component.create(m_engine->rootContext());
        m_full = qobject_cast<QQuickWindow *>(object);
        if (!m_full) {
            qWarning("%s", qPrintable(component.errorString()));
            delete object;
            return;
        }
        connect(m_full, &QWindow::visibleChanged, this, &Shell::fullVisibleChanged);
    }
    m_fullOpen = true;
    Q_EMIT fullChanged();
    m_full->showMaximized();
    m_full->requestActivate();
}

void Shell::dismissGuest()
{
    // The centre is already someone else's, or Kadunce's again: the lease is
    // not this program's to end.
    m_holdsGuest = false;
    m_launchToken.clear();
    if (!m_handedOver && m_card)
        QMetaObject::invokeMethod(m_card, "finish");
}

void Shell::completeGuestLaunch(const QString &requestToken)
{
    if (requestToken.isEmpty() || requestToken != m_launchToken)
        return;
    // Kadunce ends the lease itself once the full window has the centre.
    m_holdsGuest = false;
    finishHandOver();
}

void Shell::endGuest()
{
    if (!m_holdsGuest)
        return;
    m_holdsGuest = false;
    m_launchToken.clear();
    ask(m_bus, this, QStringLiteral("endLauncherGuest"), {}, 500, [](const QDBusMessage &) {});
    setGuestMask();
}

void Shell::cardFinished()
{
    if (m_card)
        m_card->hide();
    endGuest();
    if (!m_fullOpen)
        Q_EMIT quitRequested();
}

void Shell::fullVisibleChanged(bool visible)
{
    if (visible)
        return;
    m_fullOpen = false;
    Q_EMIT fullChanged();
    if (!m_card || !m_card->isVisible())
        Q_EMIT quitRequested();
}

} // namespace splitrock
