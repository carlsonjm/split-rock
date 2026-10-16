// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Assistants.h"
#include "app/Product.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;

namespace {

QJsonObject fixture()
{
    QFile file(QStringLiteral(FIXTURES_DIR "/auth-methods.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

Assistant byId(const QList<Assistant> &list, const QString &id)
{
    for (const Assistant &a : list) {
        if (a.id == id)
            return a;
    }
    return {};
}

QStringList offered(const QList<SignInChoice> &choices)
{
    QStringList ids;
    for (const SignInChoice &c : choices) {
        if (c.how != SignInChoice::How::NotOffered)
            ids.append(c.method.id);
    }
    return ids;
}

} // namespace

bool keyReasonNamesGenie()
{
    const QList<Assistant> list = realAssistants(QStringLiteral("/nonexistent/a"), QStringLiteral("/nonexistent/s"));
    const QJsonArray methods{QJsonObject{{QStringLiteral("id"), QStringLiteral("codex-api-key")},
                                         {QStringLiteral("name"), QStringLiteral("Key")}}};
    return classifySignIn(list.first(), acp::parseAuthMethods(methods)).first().whyNot.contains(QStringLiteral("Genie"));
}

class TestAssistants : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void setupOrder()
    {
        const QList<Assistant> list = realAssistants(QStringLiteral("/nonexistent"), QStringLiteral("/nonexistent/state"));
        // Codex alone: Claude and Gemini were never tested to ship.
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).id, QStringLiteral("codex"));
        for (const Assistant &a : list)
            QVERIFY2(!a.unavailable.isEmpty(), "an assistant that was never fetched cannot start");
    }

    // What the pinned adapters really advertised, as Split Rock treats it.
    void realSignInChoices()
    {
        const QJsonObject methods = fixture();
        QVERIFY(!methods.isEmpty());
        const QList<Assistant> list = realAssistants(QStringLiteral("/nonexistent/a"), QStringLiteral("/nonexistent/s"));

        const auto codex = classifySignIn(byId(list, QStringLiteral("codex")),
                                          acp::parseAuthMethods(methods.value(QStringLiteral("codex")).toArray()));
        QCOMPARE(offered(codex), QStringList{QStringLiteral("chatgpt")});
        QCOMPARE(codex.first().how, SignInChoice::How::Agent);
    }

    void eachAssistantHasItsOwnFolder()
    {
        const QList<Assistant> list = realAssistants(QStringLiteral("/nonexistent/a"), QStringLiteral("/nonexistent/s"));
        QCOMPARE(byId(list, QStringLiteral("codex")).environment.value(QStringLiteral("CODEX_HOME")), QStringLiteral("/nonexistent/s/codex"));
    }

    void environmentIsCleaned()
    {
        QProcessEnvironment base;
        base.insert(QStringLiteral("PATH"), QStringLiteral("/usr/bin"));
        base.insert(QStringLiteral("ANTHROPIC_API_KEY"), QStringLiteral("x"));
        base.insert(QStringLiteral("OPENAI_API_KEY"), QStringLiteral("x"));
        base.insert(QStringLiteral("GEMINI_API_KEY"), QStringLiteral("x"));
        base.insert(QStringLiteral("CLAUDECODE"), QStringLiteral("1"));
        base.insert(QStringLiteral("CLAUDE_CODE_REMOTE"), QStringLiteral("1"));
        base.insert(QStringLiteral("NO_BROWSER"), QStringLiteral("1"));
        base.insert(QStringLiteral("CODEX_HOME"), QStringLiteral("/home/test/.codex"));
        const QProcessEnvironment clean = cleanEnvironment(base);
        QCOMPARE(clean.keys(), QStringList{QStringLiteral("PATH")});

        const Assistant codex = realAssistants(QStringLiteral("/nonexistent/a"), QStringLiteral("/nonexistent/s")).first();
        const acp::LaunchSpec spec = serveLaunch(codex, base, QStringLiteral("/tmp"));
        QCOMPARE(spec.environment.value(QStringLiteral("CODEX_HOME")), QStringLiteral("/nonexistent/s/codex"));
        // Describing a start creates nothing.
        QVERIFY(!QFileInfo::exists(QStringLiteral("/nonexistent")));
        QVERIFY(!spec.environment.contains(QStringLiteral("OPENAI_API_KEY")));
    }

    void signInCannotLeaveTheAssistantsFolder()
    {
        Assistant a = pretendAssistant(QStringLiteral("/p"), QStringLiteral("/s.json"), QStringLiteral("/nonexistent/state"));
        acp::AuthMethod method;
        method.kind = acp::AuthMethod::Kind::Terminal;
        method.terminalArgs = {QStringLiteral("--sign-in"), QStringLiteral("ok")};
        method.terminalEnv = {{QStringLiteral("SPLIT_ROCK_PRETEND_HOME"), QStringLiteral("/elsewhere")},
                              {QStringLiteral("EXTRA"), QStringLiteral("1")}};
        const acp::LaunchSpec spec = signInLaunch(a, method, {}, QStringLiteral("/tmp"));
        QVERIFY(!QFileInfo::exists(QStringLiteral("/nonexistent")));
        QCOMPARE(spec.arguments, (QStringList{QStringLiteral("/s.json"), QStringLiteral("--sign-in"), QStringLiteral("ok")}));
        QCOMPARE(spec.environment.value(QStringLiteral("SPLIT_ROCK_PRETEND_HOME")), QStringLiteral("/nonexistent/state/pretend"));
        QCOMPARE(spec.environment.value(QStringLiteral("EXTRA")), QStringLiteral("1"));
    }

    void theNameFollowsWhereItRuns()
    {
        QCOMPARE(productName(), QStringLiteral("Split Rock"));
        QCOMPARE(productTitle(), QStringLiteral("Split Rock"));
        setInsideShuffle(true);
        QCOMPARE(productName(), QStringLiteral("Genie"));
        QCOMPARE(productTitle(), QStringLiteral("Shuffle"));
        QVERIFY(keyReasonNamesGenie());
        setInsideShuffle(false);
        // The test home has no Shuffle in it.
        QVERIFY(!shuffleInstalled());
    }

    void theMarkIsReadFromItsOneFile()
    {
        QFile file(QStringLiteral(PROJECT_DIR "/assets/mark.svg"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString path = markPath(QString::fromUtf8(file.readAll()));
        // The lighthouse, its window and the two glints.
        QCOMPARE(path.count(QLatin1Char('M')), 4);
        QVERIFY(path.startsWith(QStringLiteral("M 7.397061 5.6155854")));
        QVERIFY(path.endsWith(QStringLiteral("18.4595122 8.097561 18.8292683 8.097561 Z")));
    }

    void leftoversAreRemovedOnlyInsideTheFolder()
    {
        QTemporaryDir root;
        const QString outside = root.filePath(QStringLiteral("outside"));
        QDir().mkpath(outside + QStringLiteral("/sessions"));
        Assistant a;
        a.home = root.filePath(QStringLiteral("home"));
        a.conversationLeftovers = {QStringLiteral("sessions"), QStringLiteral("state_*.sqlite*"),
                                   QStringLiteral(".gemini/tmp"), QStringLiteral("../outside/sessions")};
        QDir().mkpath(a.home + QStringLiteral("/sessions/2026"));
        QDir().mkpath(a.home + QStringLiteral("/.gemini/tmp/x"));
        for (const QString name : {QStringLiteral("state_5.sqlite"), QStringLiteral("state_5.sqlite-wal"),
                                   QStringLiteral("auth.json"), QStringLiteral("sessions/2026/one.jsonl")}) {
            QFile f(a.home + QLatin1Char('/') + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        // A link out of the folder is left alone, and so is what it points at.
        QFile::link(outside, a.home + QStringLiteral("/state_9.sqlite"));

        removeConversationLeftovers(a);
        QVERIFY(!QFileInfo::exists(a.home + QStringLiteral("/sessions")));
        QVERIFY(!QFileInfo::exists(a.home + QStringLiteral("/state_5.sqlite")));
        QVERIFY(!QFileInfo::exists(a.home + QStringLiteral("/state_5.sqlite-wal")));
        QVERIFY(!QFileInfo::exists(a.home + QStringLiteral("/.gemini/tmp")));
        QVERIFY(QFileInfo::exists(a.home + QStringLiteral("/.gemini")));
        QVERIFY(QFileInfo::exists(a.home + QStringLiteral("/auth.json")));
        QVERIFY(QFileInfo(a.home + QStringLiteral("/state_9.sqlite")).isSymLink());
        QVERIFY(QFileInfo::exists(outside + QStringLiteral("/sessions")));
    }
};

QTEST_GUILESS_MAIN(TestAssistants)
#include "test_assistants.moc"
