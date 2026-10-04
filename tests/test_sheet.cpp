// SPDX-License-Identifier: GPL-2.0-or-later
//
// Genie's card and full window, shown off screen and driven against the
// pretend assistant, and against a stand-in Kadunce on a bus of the test's own.
#include "app/Conversation.h"
#include "FakeKadunce.h"
#include "PretendFixture.h"
#include "app/Notebook.h"
#include "app/QmlContext.h"
#include "desktop/Shell.h"
#include "guide/GuideLibrary.h"

#include <QDir>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

using namespace splitrock;

class TestSheet : public QObject
{
    Q_OBJECT

    // Searches what is on screen, including what Repeaters and Loaders made.
    static QQuickItem *find(QQuickItem *item, const QString &name)
    {
        if (item->objectName() == name)
            return item;
        for (QQuickItem *child : item->childItems()) {
            if (QQuickItem *found = find(child, name))
                return found;
        }
        return nullptr;
    }

    static QQuickItem *find(QQuickWindow *window, const QString &name)
    {
        return find(window->contentItem(), name);
    }

    // With SPLIT_ROCK_SCREENSHOTS set to a folder, each screen is saved there,
    // for comparing against the mock-up by eye.
    static void shoot(QQuickWindow *window, const QString &name)
    {
        const QString dir = qEnvironmentVariable("SPLIT_ROCK_SCREENSHOTS");
        if (dir.isEmpty())
            return;
        QTest::qWait(150);
        QDir().mkpath(dir);
        window->grabWindow().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
    }

    // The card loaded and opened. With no bus, it stands alone over a work
    // area the size of the mock-up's, a 1280 by 800 screen less Shuffle's
    // bottom band. With a bus, the shell asks it where to stand.
    static QQuickWindow *open(QQmlApplicationEngine &engine, Conversation &c,
                              const QDBusConnection &bus = QDBusConnection(QStringLiteral("no bus")))
    {
        auto *shell = new Shell(&engine, bus, &engine);
        engine.rootContext()->setContextProperty(QStringLiteral("conversation"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("shell"), shell);
        exposeProduct(engine.rootContext());
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        if (!window)
            return nullptr;
        if (!bus.isConnected()) {
            window->setGeometry(0, 0, 1280, 736);
            window->show();
        }
        shell->open(window);
        return QTest::qWaitForWindowExposed(window) ? window : nullptr;
    }

    static Shell *shellOf(QQmlApplicationEngine &engine)
    {
        return qobject_cast<Shell *>(engine.rootContext()->contextProperty(QStringLiteral("shell")).value<QObject *>());
    }

    // Every piece of text drawn in the window that runs past either side of
    // it, by its name or its first words.
    static QStringList overflowing(QQuickWindow *window)
    {
        QStringList found;
        const std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item->isVisible() || item->opacity() <= 0.01)
                return;
            if (item->inherits("QQuickText") || item->inherits("QQuickTextInput")) {
                const QString text = item->property("text").toString();
                const qreal drawn = item->inherits("QQuickText") ? item->property("contentWidth").toReal()
                                                                  : item->width();
                const QRectF scene = item->mapRectToScene(QRectF(0, 0, std::min(drawn, item->width() > 0 ? std::max(drawn, item->width()) : drawn), item->height()));
                if (!text.isEmpty() && (scene.left() < -0.5 || scene.right() > window->width() + 0.5))
                    found.append(QStringLiteral("%1 [%2..%3]").arg(text.left(40)).arg(scene.left()).arg(scene.right()));
            }
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        walk(window->contentItem());
        return found;
    }

    // Waits for the card's motion to settle.
    static void settle() { QTest::qWait(400); }

private Q_SLOTS:
    void initTestCase()
    {
        if (qEnvironmentVariable("SPLIT_ROCK_SCREENSHOTS_THEME") == QLatin1String("dark")) {
            QPalette dark;
            dark.setColor(QPalette::Window, QColor(0x14, 0x14, 0x14));
            dark.setColor(QPalette::WindowText, QColor(0xF8, 0xF8, 0xFF));
            dark.setColor(QPalette::Base, QColor(0x1C, 0x1C, 0x1C));
            dark.setColor(QPalette::Text, QColor(0xF8, 0xF8, 0xFF));
            dark.setColor(QPalette::Button, QColor(0x24, 0x24, 0x24));
            dark.setColor(QPalette::ButtonText, QColor(0xF8, 0xF8, 0xFF));
            dark.setColor(QPalette::Highlight, QColor(0x3D, 0xAE, 0xE9));
            QGuiApplication::setPalette(dark);
        }
        // Any warning from the sheet is a fault in it.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("qrc:/")));
    }

    void signInScreen()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("needs-sign-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);
        shoot(window, QStringLiteral("1-choose"));
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_COMPARE(c.phase(), QStringLiteral("signIn"));
        QTRY_VERIFY(find(window, QStringLiteral("signIn-pretend-browser")));
        QVERIFY(!find(window, QStringLiteral("signIn-pretend-env")));
        shoot(window, QStringLiteral("2-sign-in"));
    }

    void theGuideAnswersInTheSheet()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        GuideLibrary guide;
        guide.load({QStringLiteral(PROJECT_DIR)});
        c.setGuide(guide, {{QStringLiteral("plasma")}, {}});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);
        QTRY_VERIFY(find(window, QStringLiteral("assistant-guide")));
        shoot(window, QStringLiteral("6-guide-first"));
        c.chooseAssistant(QStringLiteral("guide"));
        QTRY_VERIFY(find(window, QStringLiteral("field")));
        shoot(window, QStringLiteral("7-guide-ready"));
        c.ask(QStringLiteral("it won't take my password"));
        QTRY_VERIFY(find(window, QStringLiteral("headline")));
        shoot(window, QStringLiteral("8-guide-answer"));
        c.ask(QStringLiteral("Show me around"));
        QTRY_COMPARE(c.headline(), QStringLiteral("Moving between virtual desktops"));
        shoot(window, QStringLiteral("9-guide-tour"));
        // The guide's answers stay in the window, for the full view.
        QCOMPARE(c.turns().size(), 1);
        QMetaObject::invokeMethod(window, "expand");
        Shell *shell = shellOf(engine);
        QTRY_VERIFY(shell->full() && shell->full()->isVisible());
        QVERIFY(find(shell->full(), QStringLiteral("fullView")));
        shell->full()->setGeometry(0, 0, 1280, 768);
        shoot(shell->full(), QStringLiteral("11-guide-full-view"));
    }

    void refusedChangeShowsDoItUnavailable()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);

        QVERIFY(find(window, QStringLiteral("assistant-pretend")));
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        QTRY_VERIFY(find(window, QStringLiteral("field")));
        QVERIFY(find(window, QStringLiteral("field"))->hasActiveFocus());

        // Typed as the person would, a key at a time.
        for (const QChar ch : QStringLiteral("Make the writing bigger"))
            QTest::keyClick(window, ch.toLatin1());
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        QTRY_VERIFY(find(window, QStringLiteral("doItCard")));
        shoot(window, QStringLiteral("4-do-it-card"));
        QQuickItem *doIt = find(window, QStringLiteral("doItButton"));
        QVERIFY(doIt);
        QVERIFY(doIt->isVisible());
        QVERIFY(!doIt->isEnabled());
        QVERIFY(find(window, QStringLiteral("showMeHowButton"))->isEnabled());

        // Not now goes back to the answer, which keeps the way back to the card.
        c.notNow();
        QTRY_VERIFY(find(window, QStringLiteral("doItPill")));
        QTRY_VERIFY(find(window, QStringLiteral("doItPill"))->isVisible());
        QVERIFY(!find(window, QStringLiteral("doItCard")));
        shoot(window, QStringLiteral("5-after-not-now"));

        c.newQuestion();
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        c.ask(QStringLiteral("How do I connect to Wi-Fi?"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        QTRY_VERIFY(find(window, QStringLiteral("headline")));
        shoot(window, QStringLiteral("3-answer"));
        QVERIFY(find(window, QStringLiteral("keepPill")));
        QVERIFY(!find(window, QStringLiteral("keepPill"))->isEnabled());
    }

    // Under an answer: Keep this, That fixed it and "Remember that?"; in the
    // full window, the kept answers, each with Forget.
    void rememberKeepAndForget()
    {
        QTemporaryDir folder;
        Notebook notebook(folder.filePath(QStringLiteral("Notebook")));
        QVERIFY(notebook.prepare(DeviceFacts::read(QStringLiteral(FIXTURES_DIR "/computer"))));
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("notebook.json"))});
        c.setNotebook(&notebook);
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        c.ask(QStringLiteral("Can you go one step at a time?"));
        QTRY_VERIFY(find(window, QStringLiteral("rememberLine")) && find(window, QStringLiteral("rememberLine"))->isVisible());
        QVERIFY(find(window, QStringLiteral("fixedPill"))->isVisible());
        shoot(window, QStringLiteral("12-remember"));
        QQuickItem *remember = find(window, QStringLiteral("rememberPill"));
        QTest::mouseClick(window, Qt::LeftButton, {}, remember->mapToScene(QPointF(remember->width() / 2, remember->height() / 2)).toPoint());
        QTRY_VERIFY(c.aboutMe().contains(QStringLiteral("one step at a time")));
        QTRY_VERIFY(!find(window, QStringLiteral("rememberLine")) || !find(window, QStringLiteral("rememberLine"))->isVisible());
        QVERIFY(c.keep());

        QMetaObject::invokeMethod(window, "expand");
        Shell *shell = shellOf(engine);
        QTRY_VERIFY(shell->full() && shell->full()->isVisible());
        QQuickWindow *full = shell->full();
        full->setGeometry(0, 0, 1280, 768);
        QQuickItem *view = find(full, QStringLiteral("fullView"));
        view->setProperty("tab", 2);
        QTRY_VERIFY(find(full, QStringLiteral("kept-0")));
        shoot(full, QStringLiteral("13-kept"));
        QMetaObject::invokeMethod(find(full, QStringLiteral("kept-0")), "acted");
        QTRY_COMPARE(c.keptNotes().size(), 0);
        view->setProperty("tab", 1);
        shoot(full, QStringLiteral("14-about-me"));
    }

    // The full window at any width a person or Bento gives it: nothing
    // drawn runs past its sides, from a wide screen down to a narrow column.
    void theFullViewFitsAnyWidth()
    {
        QTemporaryDir folder;
        Notebook notebook(folder.filePath(QStringLiteral("Notebook")));
        QVERIFY(notebook.prepare(DeviceFacts::read(QStringLiteral(FIXTURES_DIR "/computer"))));
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.setNotebook(&notebook);
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        c.ask(QStringLiteral("How do I connect to Wi-Fi?"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        c.ask(QStringLiteral("My drive won't mount and it says something about NTFS being read-only"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        QMetaObject::invokeMethod(window, "expand");
        Shell *shell = shellOf(engine);
        QTRY_VERIFY(shell->full() && shell->full()->isVisible());
        QQuickWindow *full = shell->full();
        QQuickItem *view = find(full, QStringLiteral("fullView"));
        // Bento, or the person, sizes it; it is no longer maximized.
        full->showNormal();
        for (const int width : {1280, 900, 700, 560, 420, 360}) {
            full->setGeometry(0, 0, width, 760);
            QTRY_COMPARE(full->width(), width);
            // The notebook's slide, where the width opens or folds it.
            QTest::qWait(300);
            // The notebook as the width opens it, then as the person turns it.
            for (const bool turned : {false, true}) {
                if (turned) {
                    QMetaObject::invokeMethod(view, "toggleNotebook");
                    QTest::qWait(300);
                }
                const bool open = view->property("notebookShown").toBool();
                QCOMPARE(find(full, QStringLiteral("notebook"))->isVisible(), open);
                QCOMPARE(find(full, QStringLiteral("notebookButton"))->isVisible(), !open);
                for (const int tab : {0, 1, 2}) {
                    view->setProperty("tab", tab);
                    QTest::qWait(50);
                    const QStringList past = overflowing(full);
                    QVERIFY2(past.isEmpty(), qPrintable(QStringLiteral("at %1 wide, notebook %2, tab %3: %4").arg(width)
                        .arg(open ? QStringLiteral("open") : QStringLiteral("folded")).arg(tab).arg(past.join(QStringLiteral("; ")))));
                }
                view->setProperty("tab", 0);
                shoot(full, QStringLiteral("15-full-%1-%2").arg(width).arg(open ? QStringLiteral("open") : QStringLiteral("folded")));
            }
            // Back to what the width chooses.
            view->setProperty("notebookChosen", false);
        }
        // Folded away on a wide window, the conversation keeps a reading
        // width, centred.
        full->setGeometry(0, 0, 1280, 760);
        view->setProperty("notebookChosen", true);
        view->setProperty("notebookChoice", false);
        QTest::qWait(300);
        QVERIFY(find(full, QStringLiteral("notebookButton"))->isVisible());
        shoot(full, QStringLiteral("15-full-1280-folded"));
    }

    // One card the size of Shuffle's Search, centred over the work.
    void theCardTakesSearchsShape()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);
        settle();
        QQuickItem *card = find(window, QStringLiteral("card"));
        QVERIFY(card);
        QCOMPARE(card->width(), 819.0);  // 64% of 1280
        QCOMPARE(card->height(), 471.0); // 64% of 736
        QCOMPARE(card->x(), (1280 - 819) / 2.0);
        QCOMPARE(card->y(), 133.0); // centred, rounded
        QCOMPARE(card->property("radius").toReal(), 8.0);
        QCOMPARE(find(window, QStringLiteral("header"))->height(), 44.0);
        QQuickItem *expand = find(window, QStringLiteral("expandButton"));
        QVERIFY(expand && expand->isVisible());
        // 14 from the header's edge, as Search's header controls are.
        QCOMPARE(expand->mapToItem(card, QPointF(expand->width(), 0)).x(), 819.0 - 22 - 14);

        // With the keys up the card rises just clear of them, then shortens,
        // never below 160.
        window->setProperty("keysRect", QRectF(0, 436, 1280, 300));
        settle();
        QCOMPARE(card->y() + card->height(), 436.0 - 10);
        QCOMPARE(card->height(), 416.0);
        window->setProperty("keysRect", QRectF(0, 100, 1280, 636));
        settle();
        QCOMPARE(card->height(), 160.0);
        window->setProperty("keysRect", QRectF());
        settle();
        QCOMPARE(card->height(), 471.0);
    }

    // Expand grows the card to the whole work area, less a 10 px gutter, then
    // the full window takes over: the conversation, and what is being worked
    // on.
    void expandHandsOverToTheFullWindow()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);
        Shell *shell = shellOf(engine);
        QSignalSpy quit(shell, &Shell::quitRequested);
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        c.ask(QStringLiteral("How do I connect to Wi-Fi?"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        c.ask(QStringLiteral("Make the writing bigger"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        QTRY_VERIFY(find(window, QStringLiteral("doItCard")));

        QMetaObject::invokeMethod(window, "expand");
        QQuickItem *card = find(window, QStringLiteral("card"));
        QTRY_COMPARE(card->width(), 1260.0);
        QTRY_COMPARE(card->height(), 716.0);
        QCOMPARE(card->x(), 10.0);
        QCOMPARE(card->y(), 10.0);
        QVERIFY(!find(window, QStringLiteral("expandButton"))->isVisible());

        QTRY_VERIFY(shell->full() && shell->full()->isVisible());
        QTRY_VERIFY(!window->isVisible());
        QCOMPARE(quit.size(), 0);
        QQuickWindow *full = shell->full();
        QVERIFY(find(full, QStringLiteral("fullView")));
        QVERIFY(find(full, QStringLiteral("wideField"))->isVisible());
        QVERIFY(find(full, QStringLiteral("changes")));
        // The refused change's card is still the person's to decide on.
        QVERIFY(find(full, QStringLiteral("doItCard")));
        full->setGeometry(0, 0, 1280, 768);
        shoot(full, QStringLiteral("10-full-view"));

        // Closing the full window ends the program.
        full->close();
        QTRY_COMPARE(quit.size(), 1);
    }

    // A way to sign in chosen in the card opens in the full window, so the
    // browser it brings can come in front.
    void signingInMovesToTheFullWindow()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("needs-sign-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);
        Shell *shell = shellOf(engine);
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_VERIFY(find(window, QStringLiteral("signIn-pretend-browser")));
        QQuickItem *button = find(window, QStringLiteral("signIn-pretend-browser"));
        QTest::mouseClick(window, Qt::LeftButton, {}, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QTRY_VERIFY(shell->full() && shell->full()->isVisible());
        // Nothing starts until the full window has the conversation.
        QTRY_VERIFY(!window->isVisible());
        QTRY_VERIFY(c.phase() != QLatin1String("signIn"));
    }

    // In Spread the card holds the centre, where Kadunce draws it, and
    // Kadunce answers on the card's own object.
    void inSpreadTheCardHoldsTheCentre()
    {
        PrivateBus bus;
        QVERIFY(bus.isValid());
        FakeKadunce kadunce(bus.kadunce());
        QScreen *screen = QGuiApplication::primaryScreen();
        kadunce.output = screen->name();
        const QRect area = screen->geometry();
        kadunce.card = QRect(area.x() + 100, area.y() + 60, area.width() - 200, area.height() - 160);
        kadunce.active = area.adjusted(10, 10, -10, -10);

        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c, bus.program());
        QVERIFY(window);
        Shell *shell = shellOf(engine);
        QTRY_COMPARE(shell->mode(), QStringLiteral("guest"));
        QCOMPARE(kadunce.owner, bus.program().baseService());
        QCOMPARE(kadunce.ownerPath, QStringLiteral("/SplitRock/Companion"));
        QCOMPARE(kadunce.ownerInterface, QStringLiteral("io.github.carlsonjm.SplitRock.Companion"));
        // Drawn at the reply's card rectangle, taking only its own touches.
        QQuickItem *card = find(window, QStringLiteral("card"));
        QTRY_COMPARE(card->position(), QPointF(100, 60));
        QCOMPARE(card->size(), QSizeF(area.width() - 200, area.height() - 160));
        QCOMPARE(window->mask(), QRegion(QRect(100, 60, area.width() - 200, area.height() - 160).adjusted(-16, -16, 16, 16)));

        // Expand: Kadunce grows the centre, the card grows into it, and the
        // full window is announced before it is shown.
        QMetaObject::invokeMethod(window, "expand");
        QTRY_VERIFY(kadunce.calls.contains(QStringLiteral("expand")));
        QTRY_COMPARE(card->size(), QSizeF(area.width() - 20, area.height() - 20));
        QTRY_VERIFY(kadunce.calls.contains(QStringLiteral("prepare")));
        QCOMPARE(kadunce.launching, QStringList{QLatin1String(AppId)});
        QTRY_VERIFY(shell->full() && shell->full()->isVisible());
        // The card stays until Kadunce says the full window has arrived.
        QTest::qWait(600);
        QVERIFY(window->isVisible());
        kadunce.callGuest(QStringLiteral("completeGuestLaunch"), {kadunce.token});
        QTRY_VERIFY(!window->isVisible());
        // Kadunce ends the lease itself once the full window has the centre.
        QTest::qWait(200);
        QVERIFY(!kadunce.calls.contains(QStringLiteral("end")));
    }

    // Kadunce sends dismissGuest when the centre goes to another guest or a
    // tap lands beside the card: the card closes, and leaves the lease alone.
    void dismissGuestClosesTheCard()
    {
        PrivateBus bus;
        QVERIFY(bus.isValid());
        FakeKadunce kadunce(bus.kadunce());
        QScreen *screen = QGuiApplication::primaryScreen();
        kadunce.output = screen->name();
        kadunce.card = screen->geometry().adjusted(100, 60, -100, -100);
        kadunce.active = screen->geometry().adjusted(10, 10, -10, -10);

        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        {
            QQmlApplicationEngine engine;
            QQuickWindow *window = open(engine, c, bus.program());
            QVERIFY(window);
            Shell *shell = shellOf(engine);
            QSignalSpy quit(shell, &Shell::quitRequested);
            QTRY_COMPARE(shell->mode(), QStringLiteral("guest"));
            kadunce.callGuest(QStringLiteral("dismissGuest"));
            QTRY_COMPARE(quit.size(), 1);
            QVERIFY(!kadunce.calls.contains(QStringLiteral("end")));
        }
        // Closed from the card itself, the card gives the centre back.
        kadunce.calls.clear();
        {
            QQmlApplicationEngine engine;
            QQuickWindow *window = open(engine, c, bus.program());
            QVERIFY(window);
            Shell *shell = shellOf(engine);
            QSignalSpy quit(shell, &Shell::quitRequested);
            QTRY_COMPARE(shell->mode(), QStringLiteral("guest"));
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_COMPARE(quit.size(), 1);
            QTRY_VERIFY(kadunce.calls.contains(QStringLiteral("end")));
        }
    }

    // Over an Active card there is no guest: the card stands over the work,
    // and stays up until Kadunce shows the full window as the selected card.
    void overAnActiveCardTheCardWaitsForTheFullWindow()
    {
        PrivateBus bus;
        QVERIFY(bus.isValid());
        FakeKadunce kadunce(bus.kadunce());
        kadunce.presentation = QStringLiteral("active");

        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c, bus.program());
        QVERIFY(window);
        Shell *shell = shellOf(engine);
        QTRY_COMPARE(shell->mode(), QStringLiteral("overActive"));
        QVERIFY(!kadunce.calls.contains(QStringLiteral("begin")));
        QCOMPARE(window->mask(), QRegion());

        QMetaObject::invokeMethod(window, "expand");
        QTRY_VERIFY(shell->full() && shell->full()->isVisible());
        QTest::qWait(600);
        QVERIFY(window->isVisible());
        kadunce.selectedApp = QLatin1String(AppId);
        kadunce.presentation = QStringLiteral("active");
        kadunce.announceChange();
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(shell->full()->isVisible());
    }

    // A Kadunce that refuses the centre leaves the card standing alone.
    void aRefusedCentreLeavesTheCardAlone()
    {
        PrivateBus bus;
        QVERIFY(bus.isValid());
        FakeKadunce kadunce(bus.kadunce());
        kadunce.accept = false;

        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c, bus.program());
        QVERIFY(window);
        QTRY_VERIFY(kadunce.calls.contains(QStringLiteral("begin")));
        QCOMPARE(shellOf(engine)->mode(), QStringLiteral("standalone"));
        QCOMPARE(window->mask(), QRegion());
    }

    // Esc, or a tap on the work around the card, closes the window.
    void escOrATapOutsideCloses()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        {
            QQmlApplicationEngine engine;
            QQuickWindow *window = open(engine, c);
            QVERIFY(window);
            QSignalSpy finished(window, SIGNAL(finished()));
            // A tap on the card leaves it open.
        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(640, 300));
            QTest::qWait(300);
            QCOMPARE(finished.size(), 0);
            QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(40, 40));
            QTRY_COMPARE(finished.size(), 1);
        }
        {
            QQmlApplicationEngine engine;
            QQuickWindow *window = open(engine, c);
            QVERIFY(window);
            QSignalSpy finished(window, SIGNAL(finished()));
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_COMPARE(finished.size(), 1);
        }
    }

    // On its own the card's window lies across the screen, above the rest,
    // with the card centred in the room the panels leave.
    void standsAloneAcrossTheScreen()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        auto *shell = new Shell(&engine, QDBusConnection(QStringLiteral("no bus")), &engine);
        engine.rootContext()->setContextProperty(QStringLiteral("conversation"), &c);
        engine.rootContext()->setContextProperty(QStringLiteral("shell"), shell);
        exposeProduct(engine.rootContext());
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        QVERIFY(window);
        shell->open(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(window->geometry(), window->screen()->geometry());
        QCOMPARE(shell->workArea(), window->screen()->availableGeometry().translated(-window->screen()->geometry().topLeft()));
        QVERIFY(window->flags() & Qt::FramelessWindowHint);
        QVERIFY(window->flags() & Qt::WindowStaysOnTopHint);
    }
};

QTEST_MAIN(TestSheet)
#include "test_sheet.moc"
