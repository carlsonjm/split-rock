// SPDX-License-Identifier: GPL-2.0-or-later
//
// Split Rock Settings, shown off screen against Ask on a bus of the test's
// own: Ask's switch, who answers, the model and Codex, each from its row.
#include "ask/Ask.h"
#include "ask/AskService.h"
#include "ask/LocalModel.h"
#include "settings/AskSettings.h"

#include <QDBusConnection>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;

namespace {

QQuickItem *find(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (QQuickItem *child : item->childItems()) {
        if (QQuickItem *found = find(child, name))
            return found;
    }
    return nullptr;
}

// Every word the window shows, to look for a sentence in.
QString words(QQuickItem *item)
{
    QString all = item->property("text").toString() + QLatin1Char('\n');
    for (QQuickItem *child : item->childItems())
        all += words(child);
    return all;
}

void tap(QQuickWindow *window, QQuickItem *item)
{
    const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
}

// A catalogue of one model, not downloaded.
QString catalogue(const QString &folder)
{
    QDir().mkpath(QDir(folder).filePath(QStringLiteral("models")));
    const QString path = QDir(folder).filePath(QStringLiteral("models.json"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(R"({"models": [{"id": "qwen3-4b", "name": "Qwen3 4B", "file": "small.gguf", "bytes": 2497281120,
                     "sha256": "3605803b982cb64aead44f6c1b2ae36e3acdb41d8e46c8a94c6533bc4c67e597",
                     "credit": "Qwen3 4B by the Qwen team",
                     "needs": {"memoryGiB": 0, "graphics": false},
                     "urls": ["https://example.invalid/small.gguf"]}]})");
    }
    return path;
}

} // namespace

class TestSettingsWindow : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void eachRowDoesWhatItSays()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        QTemporaryDir dir;
        LocalModel model(QStringLiteral("/bin/true"), catalogue(dir.path()), dir.filePath(QStringLiteral("models")));
        model.surveyAs(8, false);
        codex::Agent codex;
        codex.name = QStringLiteral("Codex");
        codex.home = dir.filePath(QStringLiteral("codex"));
        codex.signInFile = QStringLiteral("auth.json");
        ask::Ask::Setup setup;
        setup.engine = QStringLiteral("/bin/true");
        setup.settings = dir.filePath(QStringLiteral("split-rock-corerc"));
        setup.codex = codex;
        ask::Ask asking(&model, setup);
        ask::AskService service(&asking, &model, codex, QString());
        QVERIFY(service.publish(bus));
        QVERIFY(bus.registerService(QLatin1String(ask::AskService::Service)));

        AskSettings settings(setup.settings);
        QVERIFY(settings.installed());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("askSettings"), &settings);
        QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/qml/SettingsWindow.qml")));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create(engine.rootContext())));
        QVERIFY2(window, qPrintable(component.errorString()));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QQuickItem *root = window->contentItem();

        // Opening it reads Ask: the model by name, not downloaded, and Codex
        // installed and signed out. Nothing changes from opening it.
        QTRY_VERIFY(words(root).contains(QStringLiteral("Qwen3 4B, with Codex for bigger fixes")));
        QVERIFY(words(root).contains(QStringLiteral("not downloaded")));
        QVERIFY(words(root).contains(QStringLiteral("Sign in with ChatGPT")));
        QVERIFY(!asking.codexOnly());
        QCOMPARE(model.state(), QStringLiteral("missing"));

        // Who answers: Codex only, and back to the default.
        tap(window.get(), find(root, QStringLiteral("settings-codex-only")));
        QTRY_VERIFY(asking.codexOnly());
        QTRY_VERIFY(settings.codexOnly());
        tap(window.get(), find(root, QStringLiteral("settings-hybrid")));
        QTRY_VERIFY(!asking.codexOnly());

        // Ask's own switch, in split-rock-corerc.
        QVERIFY(asking.enabled());
        tap(window.get(), find(root, QStringLiteral("settings-ask")));
        QTRY_VERIFY(!asking.enabled());
        QVERIFY(words(root).contains(QStringLiteral("Helpers alone")));
        // Off, who answers can't be changed.
        tap(window.get(), find(root, QStringLiteral("settings-codex-only")));
        QTest::qWait(200);
        QVERIFY(!asking.codexOnly());
        tap(window.get(), find(root, QStringLiteral("settings-ask")));
        QTRY_VERIFY(asking.enabled());

        bus.unregisterService(QLatin1String(ask::AskService::Service));
    }
};

QTEST_MAIN(TestSettingsWindow)
#include "test_settings_window.moc"
