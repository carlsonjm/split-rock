// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ask Codex's window, shown off screen against the pretend assistant, with
// the change store of the test's own.
#include "app/ChangeRunner.h"
#include "app/QmlContext.h"
#include "ask/Fence.h"
#include "codex/Chat.h"
#include "PretendFixture.h"

#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;
using namespace splitrock::codex;

class TestCodexWindow : public QObject
{
    Q_OBJECT

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

    static QList<QQuickItem *> findAll(QQuickItem *item, const QString &name)
    {
        QList<QQuickItem *> found;
        if (item->objectName() == name)
            found.append(item);
        for (QQuickItem *child : item->childItems())
            found += findAll(child, name);
        return found;
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

    static void tap(QQuickWindow *window, QQuickItem *item)
    {
        const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
    }

    static bool fenceWorks()
    {
        QProcess probe;
        probe.start(QStringLiteral("bwrap"), ask::fenced(QStringLiteral("/usr/bin/true"), {}, {}));
        return probe.waitForFinished(5000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
    }

private Q_SLOTS:
    // The conversation under Codex's name, the change on its Do it row, and
    // the way out.
    void theWindowShowsWhoIsSpeaking()
    {
        PretendFixture fixture;
        QTemporaryDir store;
        ChangeRunner runner(store.path());
        bool keptOne = false;
        Chat chat(fixture.assistant(QStringLiteral("codex.json")), &runner, nullptr,
                  {[](const QString &, const QString &) { return true; },
                   [&keptOne](const QString &, const QString &, QString *) { return keptOne = true; }});
        if (!fenceWorks())
            chat.setFence({});
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
        exposeProduct(engine.rootContext());
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/CodexWindow.qml")));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create(engine.rootContext())));
        QVERIFY2(window, qPrintable(component.errorString()));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QCOMPARE(window->title(), QStringLiteral("Ask Codex"));

        chat.begin(QStringLiteral("my wifi keeps dropping"));
        QTRY_COMPARE_WITH_TIMEOUT(chat.phase(), QStringLiteral("ready"), 10000);
        QQuickItem *root = window->contentItem();
        QTRY_COMPARE(findAll(root, QStringLiteral("codex-turn-you")).size(), 1);
        QCOMPARE(findAll(root, QStringLiteral("codex-turn-codex")).size(), 1);
        QQuickItem *proposal = find(root, QStringLiteral("codex-proposal"));
        QVERIFY(proposal && proposal->isVisible());
        QQuickItem *doIt = find(root, QStringLiteral("codex-do-it"));
        QVERIFY(doIt && doIt->isVisible());
        QVERIFY(!find(root, QStringLiteral("codex-sign-in"))->isVisible());
        shoot(window.get(), QStringLiteral("codex-proposal"));

        tap(window.get(), doIt);
        QTRY_COMPARE_WITH_TIMEOUT(chat.proposal().value(QStringLiteral("state")).toString(), QStringLiteral("done"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(findAll(root, QStringLiteral("codex-turn-codex")).size(), 2, 10000);
        QTRY_VERIFY(find(root, QStringLiteral("codex-undo"))->isVisible());
        QVERIFY(!doIt->isVisible());
        tap(window.get(), find(root, QStringLiteral("codex-keep")));
        QVERIFY(keptOne);
        QTRY_VERIFY(!find(root, QStringLiteral("codex-keep"))->isEnabled());
        shoot(window.get(), QStringLiteral("codex-done"));

        tap(window.get(), find(root, QStringLiteral("codex-close")));
        QTRY_VERIFY(!window->isVisible());
    }

    // Signed out, the window asks for the person's tap on Sign in.
    void theWindowOffersTheSignIn()
    {
        PretendFixture fixture;
        Chat chat(fixture.assistant(QStringLiteral("codex-sign-in.json")), nullptr, nullptr, {});
        if (!fenceWorks())
            chat.setFence({});
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("chat"), &chat);
        exposeProduct(engine.rootContext());
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/CodexWindow.qml")));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create(engine.rootContext())));
        QVERIFY2(window, qPrintable(component.errorString()));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        chat.begin(QStringLiteral("Say hello after signing in"));
        QTRY_COMPARE_WITH_TIMEOUT(chat.phase(), QStringLiteral("signIn"), 10000);
        QQuickItem *button = find(window->contentItem(), QStringLiteral("codex-sign-in-button"));
        QTRY_VERIFY(button && button->isVisible());
        shoot(window.get(), QStringLiteral("codex-sign-in"));
        tap(window.get(), button);
        QTRY_COMPARE_WITH_TIMEOUT(chat.phase(), QStringLiteral("ready"), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(findAll(window->contentItem(), QStringLiteral("codex-turn-codex")).size(), 1, 10000);
    }
};

QTEST_MAIN(TestCodexWindow)
#include "test_codex_window.moc"
