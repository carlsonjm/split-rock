// SPDX-License-Identifier: GPL-2.0-or-later
//
// The Just Type sheet itself, shown off screen and driven against the pretend
// assistant.
#include "app/Conversation.h"
#include "PretendFixture.h"

#include <QDir>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
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
        engine.rootContext()->setContextProperty(QStringLiteral("jack"), &c);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        shoot(window, QStringLiteral("1-choose"));
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_COMPARE(c.phase(), QStringLiteral("signIn"));
        QTRY_VERIFY(find(window, QStringLiteral("signIn-pretend-browser")));
        QVERIFY(!find(window, QStringLiteral("signIn-pretend-env")));
        shoot(window, QStringLiteral("2-sign-in"));
    }

    void refusedChangeShowsDoItUnavailable()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("jack"), &c);
        engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
        QCOMPARE(engine.rootObjects().size(), 1);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));

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
    }
};

QTEST_MAIN(TestSheet)
#include "test_sheet.moc"
