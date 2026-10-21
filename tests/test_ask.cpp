// SPDX-License-Identifier: GPL-2.0-or-later
// Ask on this computer: one question at a time through the front desk, against
// a pretend model server, and on a bus of the test's own. No real model and
// no network are used.

#include "ask/Ask.h"
#include "ask/AskService.h"
#include "ask/LocalModel.h"
#include "PretendModel.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;
using namespace splitrock::ask;

namespace {

// A catalogue of one small model, and the model itself only when present.
QString catalogue(const QString &folder, bool present)
{
    const QString models = QDir(folder).filePath(QStringLiteral("models"));
    QDir().mkpath(models);
    if (present) {
        QFile file(QDir(models).filePath(QStringLiteral("small.gguf")));
        file.open(QIODevice::WriteOnly);
    }
    const QString path = QDir(folder).filePath(QStringLiteral("models.json"));
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(R"({"models": [{"id": "small", "file": "small.gguf", "bytes": 2497281120,
                     "sha256": "3605803b982cb64aead44f6c1b2ae36e3acdb41d8e46c8a94c6533bc4c67e597",
                     "needs": {"memoryGiB": 0, "graphics": false},
                     "urls": ["https://example.invalid/small.gguf"]}]})");
    }
    return path;
}

struct Fixture {
    QTemporaryDir dir;
    PretendModel pretend;
    std::unique_ptr<LocalModel> model;
    std::unique_ptr<Ask> ask;

    // server: whether Ask reaches the pretend model; present: whether the
    // model was downloaded.
    explicit Fixture(bool server = true, bool present = true)
    {
        model = std::make_unique<LocalModel>(QStringLiteral("/bin/true"), catalogue(dir.path(), present),
                                             dir.filePath(QStringLiteral("models")));
        model->surveyAs(8, false);
        Ask::Setup setup;
        setup.engine = QStringLiteral("/bin/true");
        setup.server = server ? pretend.url().toString() : QString();
        setup.settings = dir.filePath(QStringLiteral("split-rock-corerc"));
        setup.skillRoots = {QStringLiteral(PROJECT_DIR)};
        ask = std::make_unique<Ask>(model.get(), setup);
    }

    QVariantMap answer(const QString &question, QString *said = nullptr)
    {
        QSignalSpy answered(ask.get(), &Ask::answered);
        QSignalSpy words(ask.get(), &Ask::said);
        const uint id = ask->ask(question);
        if (!answered.wait(10000))
            return {};
        if (said) {
            for (const QList<QVariant> &chunk : words)
                *said += chunk.at(1).toString();
        }
        return answered.first().at(0).toUInt() == id ? answered.first().at(1).toMap() : QVariantMap();
    }
};

QJsonObject route(const QString &kind, const QString &target)
{
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("target"), target},
            {QStringLiteral("worry"), QStringLiteral("none")},
            {QStringLiteral("query"), QString()}};
}

} // namespace

class TestAsk : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // An answer comes as it is written, and says who gave it.
    void answersOnThisComputer()
    {
        Fixture f;
        f.pretend.answers.insert(QStringLiteral("What is KDE?"), QStringLiteral("It's the desktop you're using."));
        QString said;
        const QVariantMap result = f.answer(QStringLiteral("What is KDE?"), &said);
        QCOMPARE(said, QStringLiteral("It's the desktop you're using."));
        QCOMPARE(result.value(QStringLiteral("text")).toString(), said);
        QCOMPARE(result.value(QStringLiteral("who")).toString(), QStringLiteral("computer"));
        QCOMPARE(result.value(QStringLiteral("routed")).toString(), QStringLiteral("answer"));
        QVERIFY(result.value(QStringLiteral("helpers")).toStringList().isEmpty());
        QVERIFY(!result.value(QStringLiteral("handOff")).toBool());
    }

    // Each question is asked afresh: none sees the one before it.
    void eachQuestionIsAskedAfresh()
    {
        Fixture f;
        f.answer(QStringLiteral("What is KDE?"));
        f.answer(QStringLiteral("Where are my files?"));
        const QStringList answers = f.pretend.asked.filter(QStringLiteral("answer:"));
        QCOMPARE(answers.size(), 2);
        QCOMPARE(f.pretend.history.last(), 1);
    }

    // The Helper that fits goes under the answer, by its name; nothing opens.
    void helpersGoUnderTheAnswer()
    {
        Fixture f;
        const QString full = QStringLiteral("My disk is nearly full");
        f.pretend.routes.insert(full, route(QStringLiteral("change"), QStringLiteral("space")));
        const QVariantMap result = f.answer(full);
        QCOMPARE(result.value(QStringLiteral("helpers")).toStringList(), QStringList{QStringLiteral("space")});
        QVERIFY(result.value(QStringLiteral("text")).toString().startsWith(QStringLiteral("Free up space can help")));
        QCOMPARE(result.value(QStringLiteral("routed")).toString(), QStringLiteral("card:space"));
    }

    // A change no skill or Helper covers says where Codex is signed in.
    // Until Codex is signed in, a bigger change is answered in words, and
    // nobody is sent off to sign in.
    void biggerChangesAreAnsweredInWords()
    {
        Fixture f;
        const QString printer = QStringLiteral("Set up my printer");
        f.pretend.routes.insert(printer, route(QStringLiteral("change"), QStringLiteral("none")));
        const QVariantMap result = f.answer(printer);
        QVERIFY(!result.value(QStringLiteral("text")).toString().contains(QStringLiteral("ChatGPT")));
        QVERIFY(result.value(QStringLiteral("routed")).toString().startsWith(QStringLiteral("answer")));
        QVERIFY(!result.value(QStringLiteral("handOff")).toBool());
    }

    // The safety check needs no model: it answers before any download.
    void theSafetyCheckNeedsNoModel()
    {
        Fixture f(false, false);
        const QVariantMap result = f.answer(QStringLiteral("Microsoft called and wants me to buy gift cards to fix my computer"));
        QVERIFY(result.value(QStringLiteral("routed")).toString().startsWith(QStringLiteral("gate:")));
        QVERIFY(!result.value(QStringLiteral("needsModel")).toBool());
        QVERIFY(!result.value(QStringLiteral("text")).toString().isEmpty());
        QVERIFY(f.pretend.asked.isEmpty());
    }

    // Without the model, Ask says once what it needs, and downloads nothing.
    void saysWhatItNeedsFirst()
    {
        Fixture f(false, false);
        const QVariantMap result = f.answer(QStringLiteral("What is KDE?"));
        QVERIFY(result.value(QStringLiteral("needsModel")).toBool());
        QVERIFY(result.value(QStringLiteral("text")).toString().contains(QStringLiteral("about 2.5 GB")));
        QCOMPARE(f.model->state(), QStringLiteral("missing"));
    }

    // The Helper an ask's words name stays listed before the download.
    void helpersStayBeforeTheDownload()
    {
        Fixture f(false, false);
        const QVariantMap result = f.answer(QStringLiteral("my disk is almost full"));
        QVERIFY(result.value(QStringLiteral("needsModel")).toBool());
        QCOMPARE(result.value(QStringLiteral("helpers")).toStringList(), QStringList{QStringLiteral("space")});
        QVERIFY(f.answer(QStringLiteral("asdfghjkl")).value(QStringLiteral("routed")).toString() == QLatin1String("unclear"));
    }

    void switchedOffIsSilent()
    {
        Fixture f;
        QSettings(f.dir.filePath(QStringLiteral("split-rock-corerc")), QSettings::IniFormat)
            .setValue(QStringLiteral("Ask/Enabled"), false);
        const QVariantMap result = f.answer(QStringLiteral("What is KDE?"));
        QVERIFY(result.value(QStringLiteral("off")).toBool());
        QVERIFY(f.pretend.asked.isEmpty());
    }

    // A new question stops the one before it, which ends as cancelled.
    void aNewQuestionStopsTheOneBefore()
    {
        Fixture f;
        f.pretend.answers.insert(QStringLiteral("Where are my files?"), QStringLiteral("In Files."));
        QSignalSpy answered(f.ask.get(), &Ask::answered);
        const uint first = f.ask->ask(QStringLiteral("What is KDE?"));
        const uint second = f.ask->ask(QStringLiteral("Where are my files?"));
        QTRY_COMPARE_WITH_TIMEOUT(answered.size(), 2, 10000);
        QCOMPARE(answered.at(0).at(0).toUInt(), first);
        QVERIFY(answered.at(0).at(1).toMap().value(QStringLiteral("cancelled")).toBool());
        QCOMPARE(answered.at(1).at(0).toUInt(), second);
        QCOMPARE(answered.at(1).at(1).toMap().value(QStringLiteral("text")).toString(), QStringLiteral("In Files."));
    }

    // What the trial reads: the sorting's words and shape, and the Helpers.
    void theFramingNamesTheHelpers()
    {
        Fixture f;
        const QJsonObject framing = f.ask->framing();
        QVERIFY(framing.value(QStringLiteral("words")).toString().contains(QStringLiteral("- tidy: ")));
        QVERIFY(framing.value(QStringLiteral("words")).toString().contains(QStringLiteral("- look: ")));
        QCOMPARE(framing.value(QStringLiteral("cards")).toArray().size(), 8);
    }

    // llama-server runs in the fence: it reads the model, and the home folder
    // is hidden. A pretend engine answers with what it can see there.
    void theEngineRunsInTheFence()
    {
        QProcess probe;
        probe.start(QStringLiteral("bwrap"), {QStringLiteral("--ro-bind"), QStringLiteral("/"), QStringLiteral("/"),
                                              QStringLiteral("--"), QStringLiteral("/bin/true")});
        if (!probe.waitForFinished(5000) || probe.exitCode() != 0)
            QSKIP("bubblewrap can't make a fence here.");
        QTemporaryDir engineDir;
        const QString engine = engineDir.filePath(QStringLiteral("llama-server"));
        QFile script(engine);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"(#!/usr/bin/env python3
import http.server, json, os, sys
port = int(sys.argv[sys.argv.index("--port") + 1])
key = sys.argv[sys.argv.index("--api-key") + 1]
class Reply(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200); self.end_headers(); self.wfile.write(b'{"status":"ok"}')
    def do_POST(self):
        self.rfile.read(int(self.headers["Content-Length"]))
        home = os.path.expanduser("~")
        seen = [os.path.join(d, f)[len(home):] for d, _, fs in os.walk(home) for f in fs]
        text = ("key " if self.headers.get("Authorization") == "Bearer " + key else "no key ") + " ".join(sorted(seen))
        text += " cache-ram=" + os.environ.get("LLAMA_ARG_CACHE_RAM", "unset")
        body = "data: " + json.dumps({"choices": [{"delta": {"content": text}}]}) + "\n\ndata: [DONE]\n\n"
        self.send_response(200); self.send_header("Content-Type", "text/event-stream"); self.end_headers()
        self.wfile.write(body.encode())
    def log_message(self, *args): pass
http.server.HTTPServer(("127.0.0.1", port), Reply).serve_forever()
)");
        script.close();
        script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

        // The model and something private, both in the home folder.
        const QString home = QDir::homePath();
        const QString models = QDir(home).filePath(QStringLiteral(".local/share/split-rock/models"));
        QDir().mkpath(models);
        QFile(QDir(models).filePath(QStringLiteral("small.gguf"))).open(QIODevice::WriteOnly);
        QFile secret(QDir(home).filePath(QStringLiteral("private.txt")));
        QVERIFY(secret.open(QIODevice::WriteOnly));
        secret.close();

        QTemporaryDir dir;
        LocalModel model(engine, catalogue(dir.path(), false), models);
        model.surveyAs(8, false);
        QVERIFY(model.isReady());
        Ask::Setup setup;
        setup.engine = engine;
        setup.settings = dir.filePath(QStringLiteral("split-rock-corerc"));
        Ask ask(&model, setup);
        QSignalSpy answered(&ask, &Ask::answered);
        ask.ask(QStringLiteral("What is KDE?"));
        QVERIFY(answered.wait(20000));
        const QString text = answered.first().at(1).toMap().value(QStringLiteral("text")).toString();
        QVERIFY2(text.startsWith(QStringLiteral("key ")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("/small.gguf")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("private.txt")), qPrintable(text));
        // Each slot keeps only its own memory, with no second cache.
        QVERIFY2(text.contains(QStringLiteral("cache-ram=0")), qPrintable(text));
        QFile::remove(secret.fileName());
    }

    // On the bus: a question, its words and its end.
    void onTheBus()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        Fixture f;
        f.pretend.answers.insert(QStringLiteral("What is KDE?"), QStringLiteral("It's the desktop you're using."));
        AskService service(f.ask.get(), f.model.get(), codex::codex(QString(), f.dir.filePath(QStringLiteral("codex"))),
                           QStringLiteral("/bin/true"));
        QVERIFY(service.publish(bus));
        QVERIFY(bus.registerService(QLatin1String(AskService::Service)));
        QDBusInterface ask(QLatin1String(AskService::Service), QLatin1String(AskService::Path),
                           QStringLiteral("io.github.carlsonjm.SplitRock.Ask"), bus);
        QCOMPARE(QDBusReply<uint>(ask.call(QStringLiteral("ProtocolVersion"))).value(), 1u);
        QVERIFY(QDBusReply<bool>(ask.call(QStringLiteral("Enabled"))).value());
        const QVariantMap model = QDBusReply<QVariantMap>(ask.call(QStringLiteral("Model"))).value();
        QCOMPARE(model.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        QSignalSpy answered(&ask, SIGNAL(Answered(uint, QVariantMap)));
        const uint id = QDBusReply<uint>(ask.call(QStringLiteral("Ask"), QStringLiteral("What is KDE?"))).value();
        QVERIFY(id > 0);
        QTRY_COMPARE_WITH_TIMEOUT(answered.size(), 1, 10000);
        QCOMPARE(answered.first().at(0).toUInt(), id);
        const QVariantMap result = qdbus_cast<QVariantMap>(answered.first().at(1));
        QCOMPARE(result.value(QStringLiteral("text")).toString(), QStringLiteral("It's the desktop you're using."));
        QCOMPARE(result.value(QStringLiteral("who")).toString(), QStringLiteral("computer"));
        bus.unregisterService(QLatin1String(AskService::Service));
    }
};

QTEST_GUILESS_MAIN(TestAsk)
#include "test_ask.moc"
