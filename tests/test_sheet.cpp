// SPDX-License-Identifier: GPL-2.0-or-later
//
// Genie's window itself, shown off screen and driven against the pretend
// assistant.
#include "app/Conversation.h"
#include "PretendFixture.h"
#include "app/Placement.h"
#include "app/QmlContext.h"
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
#include <QTest>

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

    // The window loaded and opened over a work area the size of the mock-up's,
    // a 1280 by 800 screen less Shuffle's bottom band.
    static QQuickWindow *open(QQmlApplicationEngine &engine, Conversation &c)
    {
        engine.rootContext()->setContextProperty(QStringLiteral("conversation"), &c);
        exposeProduct(engine.rootContext());
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        if (!window)
            return nullptr;
        window->setGeometry(0, 0, 1280, 736);
        window->show();
        QMetaObject::invokeMethod(window, "open");
        return QTest::qWaitForWindowExposed(window) ? window : nullptr;
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
        QTRY_VERIFY(find(window, QStringLiteral("fullView"))->isVisible());
        shoot(window, QStringLiteral("11-guide-full-view"));
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

    // Expand grows the card to the whole work area, less a 10 px gutter, and
    // shows the full view: the conversation, and what is being worked on.
    void expandShowsTheFullView()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        QQuickWindow *window = open(engine, c);
        QVERIFY(window);
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        c.ask(QStringLiteral("How do I connect to Wi-Fi?"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        c.ask(QStringLiteral("Make the writing bigger"));
        QTRY_COMPARE(c.phase(), QStringLiteral("ready"));
        QTRY_VERIFY(find(window, QStringLiteral("doItCard")));

        QQuickItem *full = find(window, QStringLiteral("fullView"));
        QVERIFY(full && !full->isVisible());
        QMetaObject::invokeMethod(window, "expand");
        settle();
        QQuickItem *card = find(window, QStringLiteral("card"));
        QCOMPARE(card->width(), 1260.0);
        QCOMPARE(card->height(), 716.0);
        QCOMPARE(card->x(), 10.0);
        QCOMPARE(card->y(), 10.0);
        QTRY_VERIFY(full->isVisible());
        QTRY_COMPARE(full->opacity(), 1.0);
        QVERIFY(!find(window, QStringLiteral("expandButton"))->isVisible());
        QVERIFY(!find(window, QStringLiteral("field"))->isVisible());
        QVERIFY(find(window, QStringLiteral("wideField"))->isVisible());
        QQuickItem *changes = find(window, QStringLiteral("changes"));
        QVERIFY(changes);
        shoot(window, QStringLiteral("10-full-view"));
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

    // The program opens the window across the screen's work area.
    void opensOverTheWork()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("conversation"), &c);
        exposeProduct(engine.rootContext());
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        QVERIFY(window);
        openOverWork(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_COMPARE(window->geometry(), window->screen()->availableGeometry());
        QVERIFY(window->flags() & Qt::FramelessWindowHint);
    }
};

QTEST_MAIN(TestSheet)
#include "test_sheet.moc"
