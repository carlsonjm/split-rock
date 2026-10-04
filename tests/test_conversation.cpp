// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Conversation.h"
#include "PretendFixture.h"

#include <QSignalSpy>
#include <QTest>

using namespace splitrock;

class TestConversation : public QObject
{
    Q_OBJECT

    static bool reaches(Conversation &c, const QString &phase)
    {
        return QTest::qWaitFor([&] { return c.phase() == phase; }, 15000);
    }

    static int prompts(const PretendFixture &f)
    {
        return int(f.received(QStringLiteral("session/prompt")).size());
    }

private Q_SLOTS:
    void startsByChoosing()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        QVERIFY(c.choosing());
        QCOMPARE(c.phase(), QStringLiteral("choose"));
        QCOMPARE(c.assistants().size(), 1);
    }

    void unavailableAssistantCannotBeChosen()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("missing.json"))});
        QVERIFY(!c.assistants().first().toMap().value(QStringLiteral("unavailable")).toString().isEmpty());
        c.chooseAssistant(QStringLiteral("pretend"));
        QCOMPARE(c.phase(), QStringLiteral("choose"));
    }

    void askAndGetAnAnswerInShortSteps()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));

        // The assistant is put in the mode where it asks before acting.
        const auto modes = f.received(QStringLiteral("session/set_mode"));
        QCOMPARE(modes.size(), 1);
        QCOMPARE(modes.first().value(QStringLiteral("params")).toObject().value(QStringLiteral("modeId")).toString(),
                 QStringLiteral("ask-first"));

        c.ask(QStringLiteral("How do I connect to Wi-Fi?"));
        QCOMPARE(c.phase(), QStringLiteral("working"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QCOMPARE(c.question(), QStringLiteral("How do I connect to Wi-Fi?"));
        QCOMPARE(c.headline(), QStringLiteral("You connect from the network icon at the bottom right."));
        QCOMPARE(c.steps(), (QStringList{QStringLiteral("Tap the network icon."), QStringLiteral("Choose your network."),
                                         QStringLiteral("Type its password when the system asks.")}));
        QCOMPARE(c.more(), QStringList{QStringLiteral("That is all.")});
        QVERIFY(c.card().isEmpty());
        QCOMPARE(c.notice(), QString());

        // Only the first question of a conversation carries Jack's framing.
        const auto first = f.received(QStringLiteral("session/prompt")).first();
        QCOMPARE(first.value(QStringLiteral("params")).toObject().value(QStringLiteral("prompt")).toArray().size(), 2);
        c.ask(QStringLiteral("Something else"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        const auto second = f.received(QStringLiteral("session/prompt")).last();
        QCOMPARE(second.value(QStringLiteral("params")).toObject().value(QStringLiteral("prompt")).toArray().size(), 1);
        QCOMPARE(c.headline(), QStringLiteral("I have nothing scripted for that."));
    }

    void aChangeIsRefusedAndShownOnTheCard()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        c.ask(QStringLiteral("Make the writing bigger"));
        QVERIFY(reaches(c, QStringLiteral("ready")));

        QVERIFY(c.cardOpen());
        const QVariantMap card = c.card();
        QCOMPARE(card.value(QStringLiteral("heading")).toString(), QStringLiteral("Run a command on this computer"));
        QCOMPARE(card.value(QStringLiteral("why")).toString(),
                 QStringLiteral("Scaling to 125% makes everything bigger on this screen."));
        QVERIFY(card.value(QStringLiteral("password")).toString().startsWith(QStringLiteral("Needed")));
        QVERIFY(card.value(QStringLiteral("details")).toString().contains(QStringLiteral("sudo kscreen-doctor")));
        QVERIFY(!c.more().join(QLatin1Char(' ')).contains(QStringLiteral("UNEXPECTED")));
        QVERIFY(c.more().contains(QStringLiteral("I wasn't allowed to change it.")));

        c.notNow();
        QVERIFY(!c.cardOpen());
        c.openCard();
        QVERIFY(c.cardOpen());

        c.showMeHow();
        QVERIFY(!c.cardOpen());
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QCOMPARE(c.question(), QStringLiteral("Show me how instead"));
        QCOMPARE(c.steps().size(), 3);
        QVERIFY(c.card().isEmpty());
    }

    void aChangeMadeWithoutAskingIsReported()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        c.ask(QStringLiteral("do it without asking"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QVERIFY2(c.notice().contains(QStringLiteral("without asking: Edit settings.ini")), qPrintable(c.notice()));
    }

    void stopEndsTheAnswer()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        c.ask(QStringLiteral("take your time"));
        QTRY_COMPARE(c.headline(), QStringLiteral("Thinking about it"));
        c.stop();
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QCOMPARE(c.notice(), QStringLiteral("Stopped."));
        QCOMPARE(f.received(QStringLiteral("session/cancel")).size(), 1);
    }

    void anAssistantThatStopsIsAProblem()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        c.ask(QStringLiteral("crash"));
        QVERIFY(reaches(c, QStringLiteral("problem")));
        QCOMPARE(c.problem(), QStringLiteral("Pretend assistant stopped."));
        QVERIFY(c.problemDetails().contains(QStringLiteral("code 3")));

        // Trying again starts it over.
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QCOMPARE(c.problem(), QString());
    }

    void aFailedAnswerSaysSo()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        c.ask(QStringLiteral("file a request"));
        QTRY_VERIFY(c.notice().contains(QStringLiteral("busy")));
        QCOMPARE(c.phase(), QStringLiteral("ready"));
    }

    void signInThroughTheAgent()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("needs-sign-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("signIn")));

        QStringList offered;
        for (const QVariant &v : c.signInChoices())
            offered.append(v.toMap().value(QStringLiteral("id")).toString());
        QCOMPARE(offered, (QStringList{QStringLiteral("pretend-browser"), QStringLiteral("pretend-terminal")}));
        QCOMPARE(c.notOffered().size(), 3);

        // A method that is not offered cannot be used.
        c.signIn(QStringLiteral("pretend-env"));
        QCOMPARE(c.phase(), QStringLiteral("signIn"));

        c.signIn(QStringLiteral("pretend-browser"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QCOMPARE(f.received(QStringLiteral("authenticate")).size(), 1);
        c.ask(QStringLiteral("hello"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QCOMPARE(c.headline(), QStringLiteral("Hello after signing in."));
    }

    void signInWithoutATerminal()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("needs-sign-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("signIn")));
        c.signIn(QStringLiteral("pretend-terminal"));
        QVERIFY(c.signingInWithoutTerminal());
        QVERIFY(reaches(c, QStringLiteral("ready")));
        // The agent was never asked to authenticate; its own program did it.
        QCOMPARE(f.received(QStringLiteral("authenticate")).size(), 0);
        QVERIFY(QFileInfo::exists(f.home() + QStringLiteral("/signed-in")));
    }

    void failedAndAbandonedSignIns()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("sign-in-fails.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("signIn")));

        c.signIn(QStringLiteral("pretend-browser"));
        QVERIFY(reaches(c, QStringLiteral("signIn")));
        QCOMPARE(c.notice(), QStringLiteral("Signing in didn't finish. You can try again."));

        c.signIn(QStringLiteral("pretend-terminal"));
        QCOMPARE(c.phase(), QStringLiteral("signingIn"));
        QVERIFY(reaches(c, QStringLiteral("signIn")));
        QCOMPARE(c.notice(), QStringLiteral("Signing in didn't finish. You can try again."));

        c.signIn(QStringLiteral("pretend-waits"));
        QTRY_COMPARE(c.signInPage(), QStringLiteral("https://pretend.invalid/sign-in?step=1"));
        c.cancelSignIn();
        QCOMPARE(c.phase(), QStringLiteral("signIn"));

        c.signIn(QStringLiteral("pretend-agent-waits"));
        QCOMPARE(c.phase(), QStringLiteral("signingIn"));
        c.cancelSignIn();
        QVERIFY(reaches(c, QStringLiteral("signIn")));
    }

    void noWayInWithoutATerminalOrKey()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("no-way-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("problem")));
        QVERIFY(c.problem().contains(QStringLiteral("can't offer it")));
        QVERIFY(c.problemDetails().contains(QStringLiteral("Use PRETEND_KEY")));
    }

    void anotherProtocolVersionIsAProblem()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("wrong-version.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("problem")));
        QVERIFY(c.problemDetails().contains(QStringLiteral("version 2")));
    }

    void aSubscriptionIsNamed()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("subscription.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("signIn")));
        QVERIFY(c.notice().contains(QStringLiteral("A Claude subscription can't be used in Jack.")));
    }

    void anEndedConversationLeavesNothing()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        const QString transcript = f.home() + QStringLiteral("/transcripts");
        {
            Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
            c.chooseAssistant(QStringLiteral("pretend"));
            QVERIFY(reaches(c, QStringLiteral("ready")));
            c.ask(QStringLiteral("How do I connect to Wi-Fi?"));
            QVERIFY(reaches(c, QStringLiteral("ready")));
            QVERIFY(QFileInfo::exists(transcript));

            c.newQuestion();
            QVERIFY(!QFileInfo::exists(transcript));
            QCOMPARE(c.question(), QString());
            QCOMPARE(c.headline(), QString());
            QVERIFY(reaches(c, QStringLiteral("ready")));
            QCOMPARE(f.received(QStringLiteral("session/new")).size(), 2);

            c.ask(QStringLiteral("How do I connect to Wi-Fi?"));
            QVERIFY(reaches(c, QStringLiteral("ready")));
            QVERIFY(QFileInfo::exists(transcript));
        }
        // Closing the program ends the conversation too.
        QVERIFY(!QFileInfo::exists(transcript));
        QVERIFY(prompts(f) >= 2);
    }
};

QTEST_GUILESS_MAIN(TestConversation)
#include "test_conversation.moc"
