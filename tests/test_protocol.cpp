// SPDX-License-Identifier: GPL-2.0-or-later
#include "acp/Client.h"
#include "PretendFixture.h"

#include <QSignalSpy>
#include <QTest>

using namespace splitrock;

#define QVERIFY_OK(expr)                                                     \
    do {                                                                     \
        const std::optional<acp::RpcError> error_ = (expr);                  \
        QVERIFY2(!error_, error_ ? qPrintable(error_->message) : "");        \
    } while (false)

class TestProtocol : public QObject
{
    Q_OBJECT

    // Waits for a reply handler to run.
    template<typename Call>
    std::optional<acp::RpcError> wait(Call call)
    {
        bool done = false;
        std::optional<acp::RpcError> result;
        call([&](const std::optional<acp::RpcError> &error) {
            result = error;
            done = true;
        });
        if (!QTest::qWaitFor([&] { return done; }, 10000))
            return acp::RpcError{-1, QStringLiteral("timed out"), {}};
        return result;
    }

private Q_SLOTS:
    void initializeDeclaresNoFileOrTerminalAccess()
    {
        PretendFixture fixture;
        QVERIFY(fixture.isValid());
        acp::Client client;
        client.start(serveLaunch(fixture.assistant(QStringLiteral("needs-sign-in.json")), {}, fixture.state.path()));
        QVERIFY_OK(wait([&](auto done) { client.initialize(QStringLiteral("test"), done); }));
        QCOMPARE(client.agentInfo().title, QStringLiteral("Pretend assistant"));
        QCOMPARE(client.authMethods().size(), 5);

        const QJsonObject params = fixture.received(QStringLiteral("initialize")).first().value(QStringLiteral("params")).toObject();
        QCOMPARE(params.value(QStringLiteral("protocolVersion")).toInt(), 1);
        const QJsonObject caps = params.value(QStringLiteral("clientCapabilities")).toObject();
        QCOMPARE(caps.value(QStringLiteral("fs")).toObject().value(QStringLiteral("writeTextFile")).toBool(true), false);
        QCOMPARE(caps.value(QStringLiteral("fs")).toObject().value(QStringLiteral("readTextFile")).toBool(true), false);
        QCOMPARE(caps.value(QStringLiteral("terminal")).toBool(true), false);
    }

    void otherProtocolVersionIsRefused()
    {
        PretendFixture fixture;
        QVERIFY(fixture.isValid());
        acp::Client client;
        client.start(serveLaunch(fixture.assistant(QStringLiteral("wrong-version.json")), {}, fixture.state.path()));
        const auto error = wait([&](auto done) { client.initialize(QStringLiteral("test"), done); });
        QVERIFY(error);
        QVERIFY(error->message.contains(QStringLiteral("version 2")));
    }

    void sessionNeedsSignInThenWorks()
    {
        PretendFixture fixture;
        QVERIFY(fixture.isValid());
        acp::Client client;
        client.start(serveLaunch(fixture.assistant(QStringLiteral("needs-sign-in.json")), {}, fixture.state.path()));
        QVERIFY_OK(wait([&](auto done) { client.initialize(QStringLiteral("test"), done); }));
        const auto first = wait([&](auto done) { client.newSession(fixture.state.path(), done); });
        QVERIFY(first);
        QCOMPARE(first->code, acp::AuthRequiredCode);
        QVERIFY_OK(wait([&](auto done) { client.authenticate(QStringLiteral("pretend-browser"), done); }));
        QVERIFY_OK(wait([&](auto done) { client.newSession(fixture.state.path(), done); }));
        QVERIFY(!client.sessionId().isEmpty());
    }

    void permissionIsRefusedAtOnce()
    {
        PretendFixture fixture;
        QVERIFY(fixture.isValid());
        acp::Client client;
        QSignalSpy refused(&client, &acp::Client::changeRefused);
        QString said;
        connect(&client, &acp::Client::messageChunk, this, [&said](const QString &t) { said += t; });
        client.start(serveLaunch(fixture.assistant(QStringLiteral("signed-in.json")), {}, fixture.state.path()));
        QVERIFY_OK(wait([&](auto done) { client.initialize(QStringLiteral("test"), done); }));
        QVERIFY_OK(wait([&](auto done) { client.newSession(fixture.state.path(), done); }));

        QString stopReason;
        const auto error = wait([&](auto done) {
            client.prompt(QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                 {QStringLiteral("text"), QStringLiteral("Make the writing bigger")}}},
                          [&stopReason, done](const QString &reason, const std::optional<acp::RpcError> &e) {
                              stopReason = reason;
                              done(e);
                          });
        });
        QVERIFY2(!error, error ? qPrintable(error->message) : "");
        QCOMPARE(stopReason, QStringLiteral("end_turn"));
        QVERIFY2(!said.contains(QStringLiteral("UNEXPECTED")), qPrintable(said));
        QCOMPARE(refused.size(), 1);
        const auto call = refused.first().at(0).value<acp::ToolCall>();
        QCOMPARE(call.kind, QStringLiteral("execute"));
        QCOMPARE(call.command(), QStringLiteral("sudo kscreen-doctor output.1.scale.1.25"));
        QCOMPARE(refused.first().at(1).toString(), QStringLiteral("no"));

        // The reply on the wire chose the refusal.
        bool sawRefusal = false;
        for (const QJsonObject &m : fixture.received()) {
            const QJsonObject outcome = m.value(QStringLiteral("result")).toObject().value(QStringLiteral("outcome")).toObject();
            if (outcome.value(QStringLiteral("optionId")).toString() == QLatin1String("no"))
                sawRefusal = true;
            QVERIFY(outcome.value(QStringLiteral("optionId")).toString() != QLatin1String("yes"));
            QVERIFY(outcome.value(QStringLiteral("optionId")).toString() != QLatin1String("always"));
        }
        QVERIFY(sawRefusal);
    }

    void withoutARefusalOptionTheRequestIsWithdrawn()
    {
        PretendFixture fixture;
        QVERIFY(fixture.isValid());
        acp::Client client;
        QSignalSpy refused(&client, &acp::Client::changeRefused);
        QString said;
        connect(&client, &acp::Client::messageChunk, this, [&said](const QString &t) { said += t; });
        client.start(serveLaunch(fixture.assistant(QStringLiteral("signed-in.json")), {}, fixture.state.path()));
        QVERIFY_OK(wait([&](auto done) { client.initialize(QStringLiteral("test"), done); }));
        QVERIFY_OK(wait([&](auto done) { client.newSession(fixture.state.path(), done); }));
        wait([&](auto done) {
            client.prompt(QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                 {QStringLiteral("text"), QStringLiteral("only a cancel")}}},
                          [done](const QString &, const std::optional<acp::RpcError> &e) { done(e); });
        });
        QCOMPARE(refused.size(), 1);
        QCOMPARE(refused.first().at(1).toString(), QString());
        QCOMPARE(said, QStringLiteral("Understood."));
    }

    void stopFailsWaitingRequests()
    {
        PretendFixture fixture;
        QVERIFY(fixture.isValid());
        acp::Client client;
        QSignalSpy stopped(&client, &acp::Client::stopped);
        client.start(serveLaunch(fixture.assistant(QStringLiteral("signed-in.json")), {}, fixture.state.path()));
        QVERIFY_OK(wait([&](auto done) { client.initialize(QStringLiteral("test"), done); }));
        QVERIFY_OK(wait([&](auto done) { client.newSession(fixture.state.path(), done); }));
        const auto error = wait([&](auto done) {
            client.prompt(QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                 {QStringLiteral("text"), QStringLiteral("crash")}}},
                          [done](const QString &, const std::optional<acp::RpcError> &e) { done(e); });
        });
        QVERIFY(error);
        QCOMPARE(error->code, acp::AgentStoppedCode);
        QTRY_COMPARE(stopped.size(), 1);
        QCOMPARE(stopped.first().at(1).toBool(), false);
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "test_protocol.moc"
