// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Conversation.h"
#include "PretendFixture.h"
#include "app/Fence.h"
#include "app/Notebook.h"
#include "guide/GuideLibrary.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
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

        // Only the first question of a conversation carries the framing.
        const auto first = f.received(QStringLiteral("session/prompt")).first();
        QCOMPARE(first.value(QStringLiteral("params")).toObject().value(QStringLiteral("prompt")).toArray().size(), 2);
        c.ask(QStringLiteral("Something else"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        const auto second = f.received(QStringLiteral("session/prompt")).last();
        QCOMPARE(second.value(QStringLiteral("params")).toObject().value(QStringLiteral("prompt")).toArray().size(), 1);
        QCOMPARE(c.headline(), QStringLiteral("I have nothing scripted for that."));

        // The earlier question stays in the window, for the full view.
        QCOMPARE(c.turns().size(), 1);
        const QVariantMap turn = c.turns().first().toMap();
        QCOMPARE(turn.value(QStringLiteral("question")).toString(), QStringLiteral("How do I connect to Wi-Fi?"));
        QCOMPARE(turn.value(QStringLiteral("steps")).toStringList().size(), 3);
    }

    // An assistant that works aloud, checking things between, answers with
    // what it says after its last check, not with its working.
    void theAnswerIsWhatFollowsTheLastLook()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        c.ask(QStringLiteral("My drive won't mount"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QCOMPARE(c.headline(), QStringLiteral("Your drive is mounted read-only."));
        QCOMPARE(c.steps(), (QStringList{QStringLiteral("Shut Windows down fully."),
                                         QStringLiteral("Try the drive here again.")}));
        QVERIFY(c.more().isEmpty());
    }

    // Asked while the assistant is still starting, as Search asks the moment
    // it opens: the question waits, shown, and is answered once it is ready.
    void aQuestionAskedWhileStartingWaits()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.chooseAssistant(QStringLiteral("pretend"));
        QCOMPARE(c.phase(), QStringLiteral("starting"));
        c.ask(QStringLiteral("How do I connect to Wi-Fi?"));
        QVERIFY(c.waiting());
        QCOMPARE(c.question(), QStringLiteral("How do I connect to Wi-Fi?"));
        QVERIFY(QTest::qWaitFor([&] { return !c.headline().isEmpty() && c.phase() == QLatin1String("ready"); }, 15000));
        QCOMPARE(c.headline(), QStringLiteral("You connect from the network icon at the bottom right."));
        QVERIFY(!c.waiting());

        // Stopped before the assistant was ready, it is never asked.
        Conversation d({f.assistant(QStringLiteral("signed-in.json"))});
        d.chooseAssistant(QStringLiteral("pretend"));
        d.ask(QStringLiteral("Make the writing bigger"));
        d.stop();
        QVERIFY(!d.waiting());
        QCOMPARE(d.question(), QString());
        QVERIFY(reaches(d, QStringLiteral("ready")));
        QTest::qWait(300);
        QCOMPARE(d.question(), QString());
    }

    // The assistant runs fenced in: it may write in the notebook, through a
    // request Split Rock allows, and anything it writes elsewhere is lost. What
    // is never written down, and Split Rock's own record, are refused.
    void theAssistantIsFencedToTheNotebook()
    {
        if (fenceProgram().isEmpty())
            QSKIP("This computer has no bubblewrap to fence the assistant in.");
        const QString folder = QDir::homePath() + QStringLiteral("/Documents/Notebook");
        QDir(folder).removeRecursively();
        QFile::remove(QDir::homePath() + QStringLiteral("/outside-the-notebook.txt"));
        Notebook notebook(folder);
        QVERIFY(notebook.prepare(DeviceFacts::read(QStringLiteral(FIXTURES_DIR "/computer"))));

        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("notebook.json"))});
        c.setNotebook(&notebook);
        c.chooseAssistant(QStringLiteral("pretend"));
        QVERIFY(reaches(c, QStringLiteral("ready")));

        c.ask(QStringLiteral("Why does my pen stop?"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QVERIFY(!c.cardOpen());
        QCOMPARE(QFile(folder + QStringLiteral("/Device/pen-stops.md")).size(), qint64(40));
        QVERIFY(c.notice().contains(QStringLiteral("wrote in the notebook: Device/pen-stops.md")));
        QVERIFY(!QFile::exists(QDir::homePath() + QStringLiteral("/outside-the-notebook.txt")));
        QVERIFY(!QFile::exists(QStringLiteral("/usr/local/split-rock-was-here")));

        // The notebook's memory went with the first question.
        const auto first = f.received(QStringLiteral("session/prompt")).first();
        const QJsonArray blocks = first.value(QStringLiteral("params")).toObject().value(QStringLiteral("prompt")).toArray();
        QCOMPARE(blocks.size(), 3);
        const QString memory = blocks.at(1).toObject().value(QStringLiteral("text")).toString();
        QVERIFY(memory.contains(QStringLiteral("This computer (the top of This computer.md)")));
        QVERIFY(memory.contains(QStringLiteral("ROG Flow Z13")));

        // Kept, it is a note; forgotten, it is in the trash.
        QVERIFY(c.keep());
        QVERIFY(c.kept());
        QCOMPARE(c.keptNotes().size(), 1);
        const QString kept = c.keptNotes().first().toMap().value(QStringLiteral("path")).toString();
        QVERIFY(kept.endsWith(QStringLiteral("/Notes/Keyboard, pen and touch/Why does my pen stop.md")));
        QVERIFY(c.forget(kept));
        QVERIFY(!QFile::exists(kept));
        QCOMPARE(c.keptNotes().size(), 0);

        c.ask(QStringLiteral("Remember my PIN"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QVERIFY(c.cardOpen());
        QVERIFY(!QFile::exists(folder + QStringLiteral("/About me.md")));

        c.ask(QStringLiteral("Change the record"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QVERIFY(c.cardOpen());
        QVERIFY(!c.more().join(QLatin1Char(' ')).contains(QStringLiteral("UNEXPECTED")));

        // "Remember that?" is Split Rock's question; only a yes writes it.
        c.ask(QStringLiteral("Can you go one step at a time?"));
        QVERIFY(reaches(c, QStringLiteral("ready")));
        QCOMPARE(c.remember(), QStringLiteral("You like one step at a time."));
        QVERIFY(c.more().isEmpty());
        QVERIFY(!QFile::exists(folder + QStringLiteral("/About me.md")));
        QVERIFY(c.rememberYes());
        QCOMPARE(c.remember(), QString());
        QVERIFY(c.aboutMe().contains(QStringLiteral("- You like one step at a time.")));

        // That fixed it: the first question of the conversation and this
        // answer's lead go into the record.
        QVERIFY(c.markFixed());
        QVERIFY(c.fixed());
        const QVariantMap fixed = c.fixedProblems().first().toMap();
        QCOMPARE(fixed.value(QStringLiteral("text")).toString(),
                 QStringLiteral("\u201CWhy does my pen stop?\u201D I will go one step at a time."));
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

        // The full view lists what was asked for and not changed.
        QCOMPARE(c.refusedChanges().size(), 1);
        QCOMPARE(c.refusedChanges().first().toMap().value(QStringLiteral("question")).toString(),
                 QStringLiteral("Make the writing bigger"));

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
        QVERIFY(c.notice().contains(QStringLiteral("A Claude subscription can't be used in Split Rock.")));
    }

    // Inside the fence the assistant reaches no browser: its request to open
    // the sign-in page reaches Split Rock, which opens it, during sign-in.
    void theSignInPageOpensFromInsideTheFence()
    {
        if (fenceProgram().isEmpty())
            QSKIP("This computer has no bubblewrap to fence the assistant in.");
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("sign-in-opens-page.json"))});
        QSignalSpy opened(&c, &Conversation::signInPageReady);
        c.chooseAssistant(QStringLiteral("pretend"));
        QTRY_COMPARE_WITH_TIMEOUT(c.phase(), QStringLiteral("signIn"), 10000);
        c.signIn(QStringLiteral("pretend-browser"));
        QTRY_COMPARE_WITH_TIMEOUT(opened.count(), 1, 10000);
        QCOMPARE(opened.first().first().toString(), QStringLiteral("https://sign-in.example/start"));
        QCOMPARE(c.signInPage(), QStringLiteral("https://sign-in.example/start"));
        QTRY_COMPARE_WITH_TIMEOUT(c.phase(), QStringLiteral("ready"), 10000);
    }

    void theGuideAnswersWithNoAccount()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        GuideLibrary guide;
        guide.load({QStringLiteral(GUIDE_FIXTURES "/system")});
        c.setGuide(guide, {{QStringLiteral("plasma")}, {}});

        // Offered first, and ready at once: nothing starts, no sign-in.
        QCOMPARE(c.assistants().first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("guide"));
        c.chooseAssistant(QStringLiteral("guide"));
        QCOMPARE(c.phase(), QStringLiteral("ready"));
        QVERIFY(c.isGuide());
        QVERIFY(f.received().isEmpty());
        QCOMPARE(c.suggestions().size(), 3);
        QCOMPARE(c.suggestions().first(), QStringLiteral("Show me around"));

        c.ask(QStringLiteral("my internet keeps cutting out"));
        QCOMPARE(c.phase(), QStringLiteral("ready"));
        QCOMPARE(c.headline(), QStringLiteral("The Wi-Fi keeps dropping"));
        QCOMPARE(c.steps(), (QStringList{QStringLiteral("Open the network icon."),
                                         QStringLiteral("Turn power saving off, then on again.")}));
        QCOMPARE(c.more(), QStringList{QStringLiteral("The radio saves power by pausing.")});

        c.ask(QStringLiteral("how do I print a letter"));
        QCOMPARE(c.headline(), QStringLiteral("Split Rock's guide has nothing on that yet."));
        QVERIFY(c.steps().isEmpty());

        // Show me how asks an assistant, and the guide has none: nothing is
        // sent, and the answer stands.
        c.ask(QStringLiteral("my internet keeps cutting out"));
        c.showMeHow();
        QCOMPARE(c.phase(), QStringLiteral("ready"));
        QCOMPARE(c.headline(), QStringLiteral("The Wi-Fi keeps dropping"));
        QVERIFY(f.received().isEmpty());

        c.newQuestion();
        QCOMPARE(c.question(), QString());
        QCOMPARE(c.phase(), QStringLiteral("ready"));

        c.ask(QStringLiteral("Show me around"));
        QCOMPARE(c.headline(), QStringLiteral("A first look"));
        QCOMPARE(c.lead(), QStringLiteral("Windows had a taskbar; so does this desktop."));
        QCOMPARE(c.steps().size(), 2);
        QCOMPARE(c.nextTour(), QString());
    }

    void aDesktopsToursComeFirstAndFollowOnFromEachOther()
    {
        PretendFixture f;
        QVERIFY(f.isValid());
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        GuideLibrary guide;
        guide.load({QStringLiteral(GUIDE_FIXTURES "/system")});
        c.setGuide(guide, {{QStringLiteral("plasma"), QStringLiteral("shuffle")}, {}});
        c.chooseAssistant(QStringLiteral("guide"));

        c.ask(QStringLiteral("give me the tour"));
        QCOMPARE(c.headline(), QStringLiteral("Your windows as cards"));
        QCOMPARE(c.nextTour(), QStringLiteral("A first look"));
        c.showTour();
        QCOMPARE(c.headline(), QStringLiteral("A first look"));
        QCOMPARE(c.question(), QStringLiteral("give me the tour"));
        QCOMPARE(c.nextTour(), QString());
        // A plain question after the tour is answered from the guide again.
        c.ask(QStringLiteral("my internet keeps cutting out"));
        QCOMPARE(c.headline(), QStringLiteral("The Wi-Fi keeps dropping"));
        QCOMPARE(c.lead(), QString());
    }

    void noGuideEntriesMeansNoGuide()
    {
        PretendFixture f;
        Conversation c({f.assistant(QStringLiteral("signed-in.json"))});
        c.setGuide(GuideLibrary(), {{QStringLiteral("plasma")}, {}});
        QCOMPARE(c.assistants().size(), 1);
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
            c.ask(QStringLiteral("Something else"));
            QVERIFY(reaches(c, QStringLiteral("ready")));
            QCOMPARE(c.turns().size(), 1);

            c.newQuestion();
            QVERIFY(!QFileInfo::exists(transcript));
            QCOMPARE(c.question(), QString());
            QVERIFY(c.turns().isEmpty());
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
