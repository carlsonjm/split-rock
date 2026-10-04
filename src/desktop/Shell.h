// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QRect>

class QQmlApplicationEngine;
class QScreen;

namespace splitrock {

// Where Genie's card stands, and how it gives way to the full window.
//
// The card is a surface of the desktop's own where the desktop offers one, as
// Search's is. With Kadunce showing Spread, the card holds Spread's centre as a
// companion guest, drawn where Kadunce says, and Kadunce answers on this
// object. Over an Active card, or on any other desktop, it stands centred over
// the work. Expand grows the card, then opens the full window, an ordinary one
// that Kadunce makes a card, and the card goes once that window has the
// conversation.
class Shell : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.SplitRock.Companion")
    // standalone, guest (in Spread's centre) or overActive.
    Q_PROPERTY(QString mode READ mode NOTIFY placeChanged)
    // The room the panels leave, in the card window's coordinates; empty when
    // the window is that room itself.
    Q_PROPERTY(QRect workArea READ workArea NOTIFY placeChanged)
    // As a guest: where Kadunce draws the card, and where it grows to.
    Q_PROPERTY(QRect guestCard READ guestCard NOTIFY placeChanged)
    Q_PROPERTY(QRect guestActive READ guestActive NOTIFY placeChanged)
    Q_PROPERTY(bool expanded READ expanded NOTIFY expandedChanged)
    Q_PROPERTY(bool fullOpen READ fullOpen NOTIFY fullChanged)

public:
    static constexpr char ObjectPath[] = "/SplitRock/Companion";

    Shell(QQmlApplicationEngine *engine, QDBusConnection bus, QObject *parent = nullptr);
    ~Shell() override;

    QString mode() const { return m_mode; }
    QRect workArea() const { return m_workArea; }
    QRect guestCard() const { return m_guestCard; }
    QRect guestActive() const { return m_guestActive; }
    bool expanded() const { return m_expanded; }
    bool fullOpen() const { return m_fullOpen; }
    QQuickWindow *full() const { return m_full; }

    // Places the card and shows it. A window already shown keeps its place,
    // and the card stands alone in it.
    void open(QQuickWindow *card);

    // Opens the full window as an ordinary window, as Split Rock's entry does,
    // or brings it forward. windowShown follows with the token once it has
    // drawn.
    void openFull(const QString &requestToken);

    // Expand: the card grows, then hands over to the full window.
    Q_INVOKABLE void expand();

public Q_SLOTS:
    // Kadunce: the centre is no longer the card's; it closes.
    Q_SCRIPTABLE void dismissGuest();
    // Kadunce: the full window has arrived and holds the centre.
    Q_SCRIPTABLE void completeGuestLaunch(const QString &requestToken);

private Q_SLOTS:
    // The card has faded after closing or handing over.
    void cardFinished();
    void fullVisibleChanged(bool visible);
    // Kadunce's workspace has changed; over an Active card, the card waits for
    // the full window to be the one shown.
    void workspaceChanged();

Q_SIGNALS:
    void placeChanged();
    void expandedChanged();
    void fullChanged();
    // The full window has the conversation, and the card is going.
    void handedOver();
    // Neither the card nor the full window is open any more.
    void quitRequested();
    // The full window, asked for with this token, has drawn its first frame.
    void windowShown(const QString &requestToken);

private:
    void decide();
    void placeStandalone(const QString &mode);
    void placeGuest(QScreen *screen, const QRect &card, const QRect &active);
    void place(QScreen *screen);
    void configureSurface(QScreen *screen);
    void setGuestMask();
    void handOver();
    void finishHandOver();
    // The full window; maximized when it takes over from the grown card.
    void showFull(bool maximized);
    void endGuest();
    QString appId() const;

    QQmlApplicationEngine *m_engine;
    QDBusConnection m_bus;
    QPointer<QQuickWindow> m_card;
    QPointer<QQuickWindow> m_full;
    QString m_mode = QStringLiteral("standalone");
    QRect m_workArea;
    QRect m_guestCard;
    QRect m_guestActive;
    bool m_layered = false;
    bool m_placed = false;
    bool m_holdsGuest = false;
    bool m_expanded = false;
    bool m_handingOver = false;
    bool m_handedOver = false;
    bool m_fullOpen = false;
    QString m_launchToken;
    quint64 m_generation = 0;
};

} // namespace splitrock
