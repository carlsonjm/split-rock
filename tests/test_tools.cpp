// SPDX-License-Identifier: GPL-2.0-or-later
// The apps' tools through the broker: the description files, the checks every
// call passes and the risk rules, against a stand-in Notes on a bus of the
// test's own. No real app, note or account.

#include "broker/BrokerService.h"
#include "broker/Toolbox.h"
#include "tools/Capabilities.h"
#include "NotesStandIn.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;
using namespace splitrock::broker;

namespace {

QByteArray notesFile()
{
    QFile file(QStringLiteral(FIXTURES_DIR "/capabilities/notes.json"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

tools::Tool notesTool(const QString &name)
{
    QString problem;
    for (const tools::Tool &tool : tools::Capabilities::parse(notesFile(), &problem)) {
        if (tool.name == name)
            return tool;
    }
    return {};
}

// What the toolbox answers, once it has.
QVariantMap answer(const std::function<void(const Toolbox::Reply &)> &ask)
{
    auto result = std::make_shared<QVariantMap>();
    auto done = std::make_shared<bool>(false);
    ask([result, done](const QVariantMap &r) {
        *result = r;
        *done = true;
    });
    if (!QTest::qWaitFor([done] { return *done; }, 5000))
        qWarning("No answer came.");
    return *result;
}

// Every line the program logs, to show what a log line holds.
QStringList logged;
void keepLog(QtMsgType, const QMessageLogContext &, const QString &text)
{
    logged.append(text);
}

} // namespace

class TestTools : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    NotesStandIn m_notes;

    QString own() const { return m_dir.filePath(QStringLiteral("own")); }
    QString system() const { return m_dir.filePath(QStringLiteral("system")); }
    void install(const QString &folder, const QByteArray &text = notesFile())
    {
        QDir().mkpath(folder);
        QFile file(QDir(folder).filePath(QStringLiteral("notes.json")));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(QDBusConnection::sessionBus().isConnected());
        QVERIFY(m_notes.publish(QStringLiteral("notes")));
    }

    void init()
    {
        QDir(own()).removeRecursively();
        QDir(system()).removeRecursively();
        m_notes.asked.clear();
        m_notes.created.clear();
    }

    void cleanupTestCase() { m_notes.unpublish(); }

    void theFileDescribesEachTool()
    {
        QString problem;
        const QList<tools::Tool> found = tools::Capabilities::parse(notesFile(), &problem);
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
        QCOMPARE(found.size(), 3);
        QCOMPARE(found.at(0).name, QStringLiteral("notes.search"));
        QCOMPARE(found.at(0).appName, QStringLiteral("Notes"));
        QCOMPARE(found.at(2).risk, QStringLiteral("write"));
        // The shape the model's form and Codex are given, from the same file.
        const QJsonObject shape = found.at(1).argumentShape();
        QCOMPARE(shape.value(QStringLiteral("additionalProperties")).toBool(true), false);
        QCOMPARE(shape.value(QStringLiteral("properties")).toObject().value(QStringLiteral("id")).toObject()
                     .value(QStringLiteral("pattern")).toString(),
                 QStringLiteral("^[A-Za-z0-9._-]{1,128}$"));
        // Through the bus and back, a tool is the same.
        const tools::Tool back = tools::Tool::fromMap(found.at(0).toMap());
        QCOMPARE(back.argumentShape(), found.at(0).argumentShape());
        // The bus address stays with the broker.
        QVERIFY(!found.at(0).toMap().contains(QStringLiteral("service")));
    }

    void aFileOutsideTheContractIsLeftOut()
    {
        QString problem;
        QByteArray wrongApp = notesFile();
        wrongApp.replace("\"notes.read\"", "\"mail.read\"");
        QVERIFY(tools::Capabilities::parse(wrongApp, &problem).isEmpty());
        QVERIFY(problem.contains(QLatin1String("mail.read")));
        QByteArray wrongType = notesFile();
        wrongType.replace("\"type\": \"u\"", "\"type\": \"a{sv}\"");
        QVERIFY(tools::Capabilities::parse(wrongType, &problem).isEmpty());
        QByteArray wrongRisk = notesFile();
        wrongRisk.replace("\"risk\": \"write\"", "\"risk\": \"maybe\"");
        QVERIFY(tools::Capabilities::parse(wrongRisk, &problem).isEmpty());
        QByteArray later = notesFile();
        later.replace("\"version\": 0", "\"version\": 1");
        QVERIFY(tools::Capabilities::parse(later, &problem).isEmpty());
    }

    void badArgumentsAreRefused()
    {
        const tools::Tool search = notesTool(QStringLiteral("notes.search"));
        const tools::Tool read = notesTool(QStringLiteral("notes.read"));
        QVariantList positional;
        QVERIFY(tools::Capabilities::check(search, {{QStringLiteral("words"), QStringLiteral("tablet")}}, &positional).isEmpty());
        // The default is filled in, as the type the method takes.
        QCOMPARE(positional, (QVariantList{QStringLiteral("tablet"), 5u}));
        QCOMPARE(positional.at(1).typeId(), int(QMetaType::UInt));
        // A model's JSON gives numbers as fractions or longs.
        QVERIFY(tools::Capabilities::check(search, {{QStringLiteral("words"), QStringLiteral("x")}, {QStringLiteral("limit"), 3.0}}, &positional).isEmpty());
        QCOMPARE(positional.at(1), QVariant(3u));

        const QList<QVariantMap> bad{
            {},
            {{QStringLiteral("words"), QStringLiteral("x")}, {QStringLiteral("folder"), QStringLiteral("work")}},
            {{QStringLiteral("words"), 7}},
            {{QStringLiteral("words"), QString(201, QLatin1Char('a'))}},
            {{QStringLiteral("words"), QStringLiteral("x")}, {QStringLiteral("limit"), 11}},
            {{QStringLiteral("words"), QStringLiteral("x")}, {QStringLiteral("limit"), 0}},
            {{QStringLiteral("words"), QStringLiteral("x")}, {QStringLiteral("limit"), 2.5}},
            {{QStringLiteral("words"), QStringLiteral("x")}, {QStringLiteral("limit"), QStringLiteral("3")}},
            {{QStringLiteral("words"), QStringLiteral("x")}, {QStringLiteral("limit"), true}},
        };
        for (const QVariantMap &arguments : bad)
            QVERIFY2(!tools::Capabilities::check(search, arguments).isEmpty(), qPrintable(QVariant(arguments).toString()));
        QVERIFY(tools::Capabilities::check(read, {{QStringLiteral("id"), QStringLiteral("n1")}}).isEmpty());
        QVERIFY(!tools::Capabilities::check(read, {{QStringLiteral("id"), QStringLiteral("../../etc/passwd")}}).isEmpty());
        QVERIFY(!tools::Capabilities::check(read, {{QStringLiteral("id"), QStringLiteral("n1\n")}}).isEmpty());
    }

    void badArgumentsNeverReachTheApp()
    {
        install(system());
        Toolbox toolbox({own(), system()}, QDBusConnection::sessionBus());
        const QVariantMap result = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.read"), {{QStringLiteral("id"), QStringLiteral("n1; rm")}}, QStringLiteral("ask"), reply);
        });
        QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
        QVERIFY(!result.value(QStringLiteral("problem")).toString().isEmpty());
        const QVariantMap unknown = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.delete"), {}, QStringLiteral("ask"), reply);
        });
        QCOMPARE(unknown.value(QStringLiteral("ok")).toBool(), false);
        QVERIFY(m_notes.asked.isEmpty());
    }

    void aReadRunsAndSaysWhatItRead()
    {
        install(system());
        Toolbox toolbox({own(), system()}, QDBusConnection::sessionBus());
        logged.clear();
        const QtMessageHandler before = qInstallMessageHandler(keepLog);
        const QVariantMap found = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.search"), {{QStringLiteral("words"), QStringLiteral("tablet")}}, QStringLiteral("ask"), reply);
        });
        const QVariantMap read = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.read"), {{QStringLiteral("id"), QStringLiteral("n1")}}, QStringLiteral("ask"), reply);
        });
        qInstallMessageHandler(before);
        QCOMPARE(found.value(QStringLiteral("ok")).toBool(), true);
        const QVariantList hits = found.value(QStringLiteral("result")).toList();
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.first().toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Connected testing"));
        QCOMPARE(read.value(QStringLiteral("tool")).toString(), QStringLiteral("notes.read"));
        QCOMPARE(read.value(QStringLiteral("appName")).toString(), QStringLiteral("Notes"));
        QVERIFY(read.value(QStringLiteral("result")).toMap().value(QStringLiteral("text")).toString().contains(QLatin1String("Wi-Fi drops")));
        QCOMPARE(m_notes.asked, (QStringList{QStringLiteral("Find"), QStringLiteral("Read")}));
        // One line a call, naming the tool and the caller, never the words.
        const QStringList lines = logged.filter(QStringLiteral("split-rock-broker: tool "));
        QCOMPARE(lines, (QStringList{QStringLiteral("split-rock-broker: tool notes.search by ask: call ok"),
                                     QStringLiteral("split-rock-broker: tool notes.read by ask: call ok")}));
        for (const QString &line : std::as_const(logged))
            QVERIFY(!line.contains(QLatin1String("tablet")) && !line.contains(QLatin1String("Wi-Fi")));
    }

    void aWriteNeverRunsWithoutItsCard()
    {
        install(system());
        Toolbox toolbox({own(), system()}, QDBusConnection::sessionBus());
        const QVariantMap card = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.create"),
                         {{QStringLiteral("text"), QStringLiteral("Wi-Fi drops after sleep: turn off power saving for the card.")}},
                         QStringLiteral("ask"), reply);
        });
        QCOMPARE(card.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(card.value(QStringLiteral("card")).toBool(), true);
        // Titled with the note's first words, and showing what it writes.
        QCOMPARE(card.value(QStringLiteral("title")).toString(), QStringLiteral("Wi-Fi drops after sleep: turn off power…"));
        QCOMPARE(card.value(QStringLiteral("args")).toMap().value(QStringLiteral("folder")).toString(), QString());
        const QString token = card.value(QStringLiteral("token")).toString();
        QCOMPARE(token.size(), 32);
        QTest::qWait(100);
        QVERIFY(m_notes.asked.isEmpty());

        // The tap: it runs once, and only once.
        const QVariantMap made = answer([&](const Toolbox::Reply &reply) { toolbox.run(token, reply); });
        QCOMPARE(made.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(made.value(QStringLiteral("result")).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("n3"));
        QCOMPARE(m_notes.created.size(), 1);
        const QVariantMap again = answer([&](const Toolbox::Reply &reply) { toolbox.run(token, reply); });
        QCOMPARE(again.value(QStringLiteral("ok")).toBool(), false);
        const QVariantMap madeUp = answer([&](const Toolbox::Reply &reply) { toolbox.run(QString(32, QLatin1Char('0')), reply); });
        QCOMPARE(madeUp.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(m_notes.created.size(), 1);

        // The app's own problem comes back as one.
        const QVariantMap elsewhere = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.create"), {{QStringLiteral("text"), QStringLiteral("x")}, {QStringLiteral("folder"), QStringLiteral("Nowhere")}},
                         QStringLiteral("ask"), reply);
        });
        const QVariantMap failed = answer([&](const Toolbox::Reply &reply) { toolbox.run(elsewhere.value(QStringLiteral("token")).toString(), reply); });
        QCOMPARE(failed.value(QStringLiteral("ok")).toBool(), false);
        QVERIFY(failed.value(QStringLiteral("problem")).toString().contains(QLatin1String("Nowhere")));
        m_notes.notes.removeLast();
    }

    void outwardAndCodexRulesHold()
    {
        QByteArray file = notesFile();
        // A made-up tool that would send a note, and a read kept from Codex.
        file.replace("\"name\": \"notes.read\",\n      \"method\": \"Read\",\n      \"risk\": \"read\",\n      \"codex\": true",
                     "\"name\": \"notes.read\",\n      \"method\": \"Read\",\n      \"risk\": \"read\",\n      \"codex\": false");
        file.replace("\"name\": \"notes.search\",\n      \"method\": \"Find\",\n      \"risk\": \"read\"",
                     "\"name\": \"notes.search\",\n      \"method\": \"Find\",\n      \"risk\": \"outward\"");
        install(system(), file);
        Toolbox toolbox({own(), system()}, QDBusConnection::sessionBus());
        const QVariantMap sent = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.search"), {{QStringLiteral("words"), QStringLiteral("x")}}, QStringLiteral("ask"), reply);
        });
        QCOMPARE(sent.value(QStringLiteral("ok")).toBool(), false);
        const QVariantMap codex = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.read"), {{QStringLiteral("id"), QStringLiteral("n1")}}, QStringLiteral("codex"), reply);
        });
        QCOMPARE(codex.value(QStringLiteral("ok")).toBool(), false);
        const QVariantMap ask = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.read"), {{QStringLiteral("id"), QStringLiteral("n1")}}, QStringLiteral("ask"), reply);
        });
        QCOMPARE(ask.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(m_notes.asked, QStringList{QStringLiteral("Read")});
    }

    void removingTheFileRemovesItsTools()
    {
        install(system());
        Toolbox toolbox({own(), system()}, QDBusConnection::sessionBus());
        QCOMPARE(toolbox.tools().size(), 3);
        const QVariantMap card = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.create"), {{QStringLiteral("text"), QStringLiteral("Kept")}}, QStringLiteral("ask"), reply);
        });
        // Notes is uninstalled: its file goes, and its tools with it, even a
        // card already shown.
        QVERIFY(QFile::remove(QDir(system()).filePath(QStringLiteral("notes.json"))));
        QCOMPARE(toolbox.tools().size(), 0);
        const QVariantMap read = answer([&](const Toolbox::Reply &reply) {
            toolbox.call(QStringLiteral("notes.read"), {{QStringLiteral("id"), QStringLiteral("n1")}}, QStringLiteral("ask"), reply);
        });
        QCOMPARE(read.value(QStringLiteral("ok")).toBool(), false);
        const QVariantMap made = answer([&](const Toolbox::Reply &reply) { toolbox.run(card.value(QStringLiteral("token")).toString(), reply); });
        QCOMPARE(made.value(QStringLiteral("ok")).toBool(), false);
        QVERIFY(m_notes.asked.isEmpty());
    }

    void thePersonsOwnFileComesFirst()
    {
        install(system());
        QByteArray mine = notesFile();
        mine.replace("\"name\": \"Notes\"", "\"name\": \"My notes\"");
        install(own(), mine);
        Toolbox toolbox({own(), system()}, QDBusConnection::sessionBus());
        const QVariantList list = toolbox.tools();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.first().toMap().value(QStringLiteral("appName")).toString(), QStringLiteral("My notes"));
    }

    void onTheBus()
    {
        install(system());
        Toolbox toolbox({own(), system()}, QDBusConnection::sessionBus());
        BrokerService service(nullptr, &toolbox);
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(service.publish(bus));
        QVERIFY(bus.registerService(QLatin1String(BrokerService::Service)));
        // From another connection, as Ask or Mail calls it.
        QDBusConnection client = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("client"));
        // Not through QDBusInterface, whose look at the interface would wait
        // on this same thread.
        const auto call = [&client](const QString &method, const QVariantList &arguments) {
            QDBusMessage request = QDBusMessage::createMethodCall(QLatin1String(BrokerService::Service), QLatin1String(BrokerService::Path),
                                                                  QStringLiteral("io.github.carlsonjm.SplitRock.Broker"), method);
            request.setArguments(arguments);
            QDBusPendingCallWatcher watcher(client.asyncCall(request, 5000));
            if (!QTest::qWaitFor([&watcher] { return watcher.isFinished(); }, 5000))
                qWarning("No answer came.");
            return watcher.reply();
        };
        QCOMPARE(call(QStringLiteral("ProtocolVersion"), {}).arguments().value(0).toUInt(), 0u);
        const QVariantList list = BrokerService::plain(call(QStringLiteral("Tools"), {}).arguments().value(0)).toList();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("notes.read"));

        const QVariantMap read = BrokerService::plain(call(QStringLiteral("Call"), {QStringLiteral("notes.read"),
            QVariantMap{{QStringLiteral("id"), QStringLiteral("n1")}}, QStringLiteral("ask")}).arguments().value(0)).toMap();
        QCOMPARE(read.value(QStringLiteral("result")).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Connected testing"));

        const QVariantMap card = BrokerService::plain(call(QStringLiteral("Call"), {QStringLiteral("notes.create"),
            QVariantMap{{QStringLiteral("text"), QStringLiteral("From the bus")}}, QStringLiteral("ask")}).arguments().value(0)).toMap();
        QCOMPARE(card.value(QStringLiteral("card")).toBool(), true);
        QCOMPARE(m_notes.created.size(), 0);
        const QVariantMap made = BrokerService::plain(call(QStringLiteral("Run"), {card.value(QStringLiteral("token"))}).arguments().value(0)).toMap();
        QCOMPARE(made.value(QStringLiteral("ok")).toBool(), true);
        QCOMPARE(m_notes.created.size(), 1);
        m_notes.notes.removeLast();
        bus.unregisterService(QLatin1String(BrokerService::Service));
        bus.unregisterObject(QLatin1String(BrokerService::Path));
        QDBusConnection::disconnectFromBus(QStringLiteral("client"));
    }
};

QTEST_GUILESS_MAIN(TestTools)
#include "test_tools.moc"
