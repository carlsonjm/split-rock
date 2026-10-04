// SPDX-License-Identifier: GPL-2.0-or-later
//
// The conversation's session-bus interface, used as Search uses it: by a client
// that is not Split Rock and loads none of its code. Each test starts the real
// Split Rock, with the pretend assistant only, on a private bus and in a home
// of its own, and drives the conversation through the bus alone.
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

const QString Service = QStringLiteral("io.github.carlsonjm.splitrock");
const QString Path = QStringLiteral("/Conversation");
const QString Interface = QStringLiteral("io.github.carlsonjm.SplitRock.Conversation");

QVariantMap asMap(const QVariant &value)
{
    if (value.canConvert<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}

} // namespace

class ConversationBusTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_home;
    QProcess m_splitRock;
    std::unique_ptr<QDBusInterface> m_conversation;

    QString state() const { return m_home->path() + QStringLiteral("/state"); }

    QProcessEnvironment environment() const
    {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        const QString home = m_home->path();
        environment.insert(QStringLiteral("HOME"), home);
        environment.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/share"));
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/config"));
        environment.insert(QStringLiteral("XDG_CACHE_HOME"), home + QStringLiteral("/cache"));
        environment.insert(QStringLiteral("XDG_STATE_HOME"), home + QStringLiteral("/state-home"));
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("QT_QUICK_BACKEND"), QStringLiteral("software"));
        environment.remove(QStringLiteral("WAYLAND_DISPLAY"));
        environment.remove(QStringLiteral("DISPLAY"));
        return environment;
    }

    QStringList arguments(const QString &script, const QString &first = QStringLiteral("--background")) const
    {
        return {first, QStringLiteral("--assistants"), m_home->path() + QStringLiteral("/no-adapters"),
                QStringLiteral("--state"), state(), QStringLiteral("--pretend"),
                QStringLiteral(SCRIPTS_DIR "/") + script};
    }

    // Starts Split Rock, as Search's first call would, with the pretend
    // assistant following this script; remembered is the assistant the person
    // chose last time.
    bool start(const QString &script, const QString &remembered = {})
    {
        m_home = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/split-rock-bus-XXXXXX"));
        if (!m_home->isValid())
            return false;
        if (!remembered.isEmpty()) {
            QDir().mkpath(m_home->path() + QStringLiteral("/config"));
            QFile rc(m_home->path() + QStringLiteral("/config/split-rockrc"));
            if (!rc.open(QIODevice::WriteOnly))
                return false;
            rc.write("[Assistant]\nchosen=" + remembered.toUtf8() + "\n");
        }
        m_splitRock.setProcessEnvironment(environment());
        m_splitRock.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_splitRock.start(QStringLiteral(SPLIT_ROCK_PROGRAM), arguments(script));
        if (!m_splitRock.waitForStarted())
            return false;
        if (!QTest::qWaitFor([] { return QDBusConnection::sessionBus().interface()->isServiceRegistered(Service).value(); },
                             15000))
            return false;
        m_conversation = std::make_unique<QDBusInterface>(Service, Path, Interface);
        return m_conversation->isValid();
    }

    void stop()
    {
        m_conversation.reset();
        if (m_splitRock.state() != QProcess::NotRunning) {
            m_splitRock.terminate();
            m_splitRock.waitForFinished(5000);
        }
        QTRY_VERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(Service).value());
    }

    QDBusMessage raw(const QString &method, const QVariantList &arguments = {})
    {
        return m_conversation->callWithArgumentList(QDBus::Block, method, arguments);
    }

    QVariantMap call(const QString &method, const QVariantList &arguments = {})
    {
        const QDBusMessage reply = raw(method, arguments);
        if (reply.type() != QDBusMessage::ReplyMessage) {
            qWarning().noquote() << method << reply.errorName() << reply.errorMessage();
            return {};
        }
        return asMap(reply.arguments().value(0));
    }

    static QVariantMap lastChange(const QSignalSpy &changed)
    {
        return changed.isEmpty() ? QVariantMap() : asMap(changed.last().value(0));
    }

private Q_SLOTS:
    void cleanup() { stop(); }

    // With no assistant chosen yet, Start says so, and Search offers Expand.
    void nothingChosenAsksToChoose()
    {
        QVERIFY(start(QStringLiteral("signed-in.json")));
        const QDBusReply<uint> version = m_conversation->call(QStringLiteral("ProtocolVersion"));
        QVERIFY(version.isValid());
        QCOMPARE(version.value(), 1u);

        const QVariantMap state = call(QStringLiteral("Start"));
        QCOMPARE(state.value(QStringLiteral("phase")).toString(), QStringLiteral("choose"));
        QCOMPARE(state.value(QStringLiteral("assistant")).toString(), QString());
        QVERIFY(!state.value(QStringLiteral("suggestions")).toStringList().isEmpty());
        for (const QString &key : {QStringLiteral("question"), QStringLiteral("answer"), QStringLiteral("doItReason"),
                                   QStringLiteral("problem")})
            QVERIFY2(state.value(key).typeId() == QMetaType::QString, qPrintable(key));
        QVERIFY(state.value(QStringLiteral("steps")).typeId() == QMetaType::QStringList);
        QVERIFY(!state.value(QStringLiteral("canDoIt")).toBool());
        QVERIFY(!state.value(QStringLiteral("kept")).toBool());

        // No question before an assistant is ready.
        QCOMPARE(raw(QStringLiteral("Ask"), {QStringLiteral("Hello")}).errorName(),
                 QStringLiteral("io.github.carlsonjm.SplitRock.Error.NotReady"));
    }

    // The assistant the person chose starts again; a question's answer
    // streams through Changed; a change it asks for is refused, with why.
    void aConversationThroughTheBus()
    {
        QVERIFY(start(QStringLiteral("signed-in.json"), QStringLiteral("pretend")));
        QSignalSpy changed(m_conversation.get(), SIGNAL(Changed(QVariantMap)));

        QVariantMap state = call(QStringLiteral("Start"));
        QCOMPARE(state.value(QStringLiteral("assistant")).toString(), QStringLiteral("Pretend assistant"));
        QVERIFY(state.value(QStringLiteral("phase")).toString() != QLatin1String("choose"));
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);

        // A second Start resumes the same conversation.
        QCOMPARE(call(QStringLiteral("Start")).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"));

        changed.clear();
        state = call(QStringLiteral("Ask"), {QStringLiteral("How do I connect to Wi-Fi?")});
        QCOMPARE(state.value(QStringLiteral("phase")).toString(), QStringLiteral("answering"));
        QCOMPARE(state.value(QStringLiteral("question")).toString(), QStringLiteral("How do I connect to Wi-Fi?"));
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        // The answer arrived while it was being written, not only at the end.
        bool streamed = false;
        for (const QList<QVariant> &signal : changed) {
            const QVariantMap during = asMap(signal.value(0));
            streamed |= during.value(QStringLiteral("phase")).toString() == QLatin1String("answering")
                && !during.value(QStringLiteral("answer")).toString().isEmpty();
        }
        QVERIFY(streamed);
        state = lastChange(changed);
        QCOMPARE(state.value(QStringLiteral("answer")).toString(),
                 QStringLiteral("You connect from the network icon at the bottom right."));
        QCOMPARE(state.value(QStringLiteral("steps")).toStringList().size(), 3);
        QCOMPARE(state.value(QStringLiteral("more")).toStringList(), QStringList{QStringLiteral("That is all.")});
        QCOMPARE(state.value(QStringLiteral("doItReason")).toString(), QString());

        // Do it is for a change; this answer asks for none.
        QCOMPARE(raw(QStringLiteral("Act"), {QStringLiteral("do-it")}).errorName(),
                 QStringLiteral("io.github.carlsonjm.SplitRock.Error.NotAvailable"));

        changed.clear();
        call(QStringLiteral("Ask"), {QStringLiteral("Make the writing bigger")});
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        state = lastChange(changed);
        QVERIFY(!state.value(QStringLiteral("canDoIt")).toBool());
        QVERIFY(state.value(QStringLiteral("doItReason")).toString().contains(QStringLiteral("can't make changes yet")));

        // Keep this: the answer becomes a note in the notebook, by topic.
        state = call(QStringLiteral("Act"), {QStringLiteral("keep")});
        QVERIFY(state.value(QStringLiteral("kept")).toBool());
        QVERIFY(QFile::exists(m_home->path() + QStringLiteral("/Documents/Notebook/Notes/Display/Make the writing bigger.md")));
        // Kept once is enough.
        QVERIFY(call(QStringLiteral("Act"), {QStringLiteral("keep")}).value(QStringLiteral("kept")).toBool());
        QCOMPARE(raw(QStringLiteral("Act"), {QStringLiteral("frobnicate")}).errorName(),
                 QStringLiteral("org.freedesktop.DBus.Error.InvalidArgs"));

        // Do it shows the card in the full window, and Do it there is
        // unavailable; the conversation is unchanged.
        state = call(QStringLiteral("Act"), {QStringLiteral("do-it")});
        QVERIFY(!state.value(QStringLiteral("canDoIt")).toBool());

        changed.clear();
        state = call(QStringLiteral("Act"), {QStringLiteral("show-me-how")});
        QCOMPARE(state.value(QStringLiteral("phase")).toString(), QStringLiteral("answering"));
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        QCOMPARE(lastChange(changed).value(QStringLiteral("steps")).toStringList().size(), 3);
        QCOMPARE(lastChange(changed).value(QStringLiteral("doItReason")).toString(), QString());

        // Cancel with nothing in progress leaves the conversation as it is.
        QCOMPARE(call(QStringLiteral("Cancel")).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"));
    }

    // Search opens its Genie mode with Start and lets the person ask at once,
    // without waiting for either reply (Tettegouche's GenieChat). The
    // question asked while the assistant is still starting is answered.
    void aQuestionAskedWhileStartingIsAnswered()
    {
        QVERIFY(start(QStringLiteral("signed-in.json"), QStringLiteral("pretend")));
        QSignalSpy changed(m_conversation.get(), SIGNAL(Changed(QVariantMap)));
        QDBusPendingCall started = m_conversation->asyncCall(QStringLiteral("Start"));
        QDBusPendingCall asked = m_conversation->asyncCall(QStringLiteral("Ask"), QStringLiteral("How do I connect to Wi-Fi?"));
        asked.waitForFinished();
        QVERIFY2(!asked.isError(), qPrintable(asked.error().message()));
        const QVariantMap reply = asMap(asked.reply().arguments().value(0));
        QCOMPARE(reply.value(QStringLiteral("question")).toString(), QStringLiteral("How do I connect to Wi-Fi?"));
        QCOMPARE(reply.value(QStringLiteral("problem")).toString(), QString());
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("answer")).toString(),
                                  QStringLiteral("You connect from the network icon at the bottom right."), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        QCOMPARE(lastChange(changed).value(QStringLiteral("steps")).toStringList().size(), 3);
    }

    // The notebook is made when Split Rock starts, and what is never written
    // down is refused when kept, with why.
    void theNotebookAndWhatIsNeverWrittenDown()
    {
        QVERIFY(start(QStringLiteral("signed-in.json"), QStringLiteral("pretend")));
        const QString notebook = m_home->path() + QStringLiteral("/Documents/Notebook");
        QVERIFY(QFile::exists(notebook + QStringLiteral("/This computer.md")));
        QVERIFY(QFile::exists(notebook + QStringLiteral("/Device/DEVICE.md")));
        QVERIFY(QFile::exists(notebook + QStringLiteral("/ASSISTANT.md")));

        QSignalSpy changed(m_conversation.get(), SIGNAL(Changed(QVariantMap)));
        call(QStringLiteral("Start"));
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        changed.clear();
        call(QStringLiteral("Ask"), {QStringLiteral("Remember my card 4111 1111 1111 1111")});
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        const QDBusMessage refused = raw(QStringLiteral("Act"), {QStringLiteral("keep")});
        QCOMPARE(refused.errorName(), QStringLiteral("io.github.carlsonjm.SplitRock.Error.Refused"));
        QVERIFY(refused.errorMessage().contains(QStringLiteral("never writes down")));
        QVERIFY(!QDir(notebook + QStringLiteral("/Notes")).exists());
    }

    // Search asks "Remember that?" with what the assistant offered, and
    // records a fix, through the same conversation.
    void rememberAndThatFixedIt()
    {
        QVERIFY(start(QStringLiteral("notebook.json"), QStringLiteral("pretend")));
        QSignalSpy changed(m_conversation.get(), SIGNAL(Changed(QVariantMap)));
        call(QStringLiteral("Start"));
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        changed.clear();
        call(QStringLiteral("Ask"), {QStringLiteral("Can you go one step at a time?")});
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        QCOMPARE(lastChange(changed).value(QStringLiteral("remember")).toString(), QStringLiteral("You like one step at a time."));

        const QString notebook = m_home->path() + QStringLiteral("/Documents/Notebook");
        QVERIFY(!QFile::exists(notebook + QStringLiteral("/About me.md")));
        QVariantMap state = call(QStringLiteral("Act"), {QStringLiteral("remember")});
        QCOMPARE(state.value(QStringLiteral("remember")).toString(), QString());
        QFile about(notebook + QStringLiteral("/About me.md"));
        QVERIFY(about.open(QIODevice::ReadOnly));
        QVERIFY(about.readAll().contains("- You like one step at a time."));
        QCOMPARE(raw(QStringLiteral("Act"), {QStringLiteral("remember")}).errorName(),
                 QStringLiteral("io.github.carlsonjm.SplitRock.Error.NotAvailable"));

        state = call(QStringLiteral("Act"), {QStringLiteral("fixed")});
        QVERIFY(state.value(QStringLiteral("fixed")).toBool());
        QFile record(notebook + QStringLiteral("/This computer.md"));
        QVERIFY(record.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(record.readAll()).contains(QStringLiteral("\u201CCan you go one step at a time?\u201D I will go one step at a time.")));
    }

    // An answer that opens with its steps still has a line to lead with, and
    // what follows the steps reaches Search too.
    void anAnswerThatOpensWithItsSteps()
    {
        QVERIFY(start(QStringLiteral("signed-in.json"), QStringLiteral("pretend")));
        QSignalSpy changed(m_conversation.get(), SIGNAL(Changed(QVariantMap)));
        call(QStringLiteral("Start"));
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        changed.clear();
        call(QStringLiteral("Ask"), {QStringLiteral("Where is my printer?")});
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        const QVariantMap state = lastChange(changed);
        QCOMPARE(state.value(QStringLiteral("answer")).toString(), QStringLiteral("That should find it."));
        QCOMPARE(state.value(QStringLiteral("steps")).toStringList().size(), 2);
        QCOMPARE(state.value(QStringLiteral("more")).toStringList(), QStringList{QStringLiteral("If not, ask me again.")});
    }

    // An assistant that is not signed in says so; the full window finishes it.
    void signingInIsForTheFullWindow()
    {
        QVERIFY(start(QStringLiteral("needs-sign-in.json"), QStringLiteral("pretend")));
        QSignalSpy changed(m_conversation.get(), SIGNAL(Changed(QVariantMap)));
        call(QStringLiteral("Start"));
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("sign-in"), 15000);
    }

    // OpenWindow opens the full window, and WindowShown says, with the
    // caller's token, when it has drawn.
    void theFullWindowSaysWhenItHasDrawn()
    {
        QVERIFY(start(QStringLiteral("signed-in.json")));
        QSignalSpy shown(m_conversation.get(), SIGNAL(WindowShown(QString)));
        const QDBusReply<bool> accepted = m_conversation->call(QStringLiteral("OpenWindow"), QStringLiteral("token-1"));
        QVERIFY(accepted.isValid() && accepted.value());
        QTRY_VERIFY_WITH_TIMEOUT(shown.count() >= 1, 10000);
        QCOMPARE(shown.first().value(0).toString(), QStringLiteral("token-1"));
        // Asked again, it is already open, and says so again.
        m_conversation->call(QStringLiteral("OpenWindow"), QStringLiteral("token-2"));
        QTRY_VERIFY_WITH_TIMEOUT(shown.count() >= 2, 10000);
        QCOMPARE(shown.last().value(0).toString(), QStringLiteral("token-2"));
    }

    // Split Rock runs once: opening it again, from its entry or its Ask
    // action, goes to the one running, and the second start ends.
    void aSecondStartGoesToTheOneRunning()
    {
        QVERIFY(start(QStringLiteral("signed-in.json")));
        const qint64 first = m_splitRock.processId();
        for (const QString &how : {QStringLiteral("--ask"), QStringLiteral("--assistants")}) {
            QProcess second;
            second.setProcessEnvironment(environment());
            second.setProcessChannelMode(QProcess::ForwardedErrorChannel);
            QStringList args = arguments(QStringLiteral("signed-in.json"), how);
            if (how == QLatin1String("--assistants"))
                args.removeFirst(); // as from the entry: no --ask, no --background
            second.start(QStringLiteral(SPLIT_ROCK_PROGRAM), args);
            QVERIFY(second.waitForFinished(15000));
            QCOMPARE(second.exitCode(), 0);
        }
        QCOMPARE(m_splitRock.state(), QProcess::Running);
        QCOMPARE(m_splitRock.processId(), first);
        QCOMPARE(raw(QStringLiteral("ProtocolVersion")).type(), QDBusMessage::ReplyMessage);
    }

    // The session ends Split Rock with a signal; it quits the usual way, and
    // the assistant's record of the conversation goes with it.
    void endingTheSessionLeavesNothing()
    {
        QVERIFY(start(QStringLiteral("signed-in.json"), QStringLiteral("pretend")));
        QSignalSpy changed(m_conversation.get(), SIGNAL(Changed(QVariantMap)));
        call(QStringLiteral("Start"));
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        changed.clear();
        call(QStringLiteral("Ask"), {QStringLiteral("How do I connect to Wi-Fi?")});
        QTRY_COMPARE_WITH_TIMEOUT(lastChange(changed).value(QStringLiteral("phase")).toString(), QStringLiteral("ready"), 15000);
        const QString transcripts = state() + QStringLiteral("/pretend/transcripts");
        QVERIFY(QFileInfo::exists(transcripts));
        m_splitRock.terminate();
        QVERIFY(m_splitRock.waitForFinished(10000));
        QCOMPARE(m_splitRock.exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_splitRock.exitCode(), 0);
        QVERIFY(!QFileInfo::exists(transcripts));
    }
};

int main(int argc, char *argv[])
{
    // Only on a bus of the test's own, as tests/CMakeLists.txt starts it.
    if (qEnvironmentVariable("SPLIT_ROCK_PRIVATE_BUS") != QLatin1String("1")) {
        fputs("Runs only on the private bus its test entry starts.\n", stderr);
        return 77;
    }
    QCoreApplication app(argc, argv);
    ConversationBusTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_conversation_bus.moc"
