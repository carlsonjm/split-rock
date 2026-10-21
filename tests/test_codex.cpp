// SPDX-License-Identifier: GPL-2.0-or-later
// Ask Codex's conversation, against the pretend assistant only: no account, no
// network and nothing on the computer to change. Its Do it card runs through
// a change store of the test's own.

#include "app/ChangeRunner.h"
#include "ask/Fence.h"
#include "codex/Chat.h"
#include "PretendFixture.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace splitrock;
using namespace splitrock::codex;

namespace {

// Whether bubblewrap can make a fence here; where it can't, the pretend
// assistant runs unfenced, and the fence's own test is skipped.
bool fenceWorks()
{
    static const bool works = [] {
        QProcess probe;
        probe.start(QStringLiteral("bwrap"), ask::fenced(QStringLiteral("/usr/bin/true"), {}, {}));
        return probe.waitForFinished(5000) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
    }();
    return works;
}

QString lastText(const Chat &chat, const QString &who)
{
    const QVariantList turns = chat.turns();
    for (qsizetype i = turns.size() - 1; i >= 0; --i) {
        if (turns.at(i).toMap().value(QStringLiteral("who")).toString() == who)
            return turns.at(i).toMap().value(QStringLiteral("text")).toString();
    }
    return {};
}

struct Kept {
    QString title;
    QString text;
    QString opened;
};

} // namespace

class TestCodex : public QObject
{
    Q_OBJECT

    std::unique_ptr<Chat> chatFor(const PretendFixture &fixture, const QString &script, ChangeRunner *runner,
                                  Kept *kept = nullptr, QStringList *cards = nullptr)
    {
        Chat::Hands hands;
        hands.openCard = [cards](const QString &card, const QString &) {
            if (cards)
                cards->append(card);
            return true;
        };
        hands.keep = [kept](const QString &title, const QString &text, QString *note, QString *) {
            if (kept)
                *kept = {title, text};
            *note = QStringLiteral("note-1");
            return true;
        };
        hands.openNote = [kept](const QString &note) {
            if (kept)
                kept->opened = note;
            return true;
        };
        auto chat = std::make_unique<Chat>(fixture.assistant(script), runner, nullptr, hands);
        if (!fenceWorks())
            chat->setFence({});
        return chat;
    }

private Q_SLOTS:
    // A question is answered under Codex's name, the change it proposes waits
    // on a Do it card, and Codex hears how the change went.
    void answersAndProposesAChange()
    {
        PretendFixture fixture;
        QTemporaryDir store;
        ChangeRunner runner(store.path());
        auto chat = chatFor(fixture, QStringLiteral("codex.json"), &runner);
        chat->begin(QStringLiteral("my wifi keeps dropping"));
        QTRY_COMPARE_WITH_TIMEOUT(chat->phase(), QStringLiteral("ready"), 10000);
        QCOMPARE(lastText(*chat, QStringLiteral("you")), QStringLiteral("my wifi keeps dropping"));
        QCOMPARE(lastText(*chat, QStringLiteral("codex")),
                 QStringLiteral("Your card's power saving is on.\n\nI've put the change on a card below."));
        // Asked first, before anything else; the framing went once.
        const auto modes = fixture.received(QStringLiteral("session/set_mode"));
        QCOMPARE(modes.size(), 1);
        QCOMPARE(modes.first().value(QStringLiteral("params")).toObject().value(QStringLiteral("modeId")).toString(),
                 QStringLiteral("ask-first"));
        const QString first = fixture.received(QStringLiteral("session/prompt")).first().value(QStringLiteral("params"))
                                  .toObject().value(QStringLiteral("prompt")).toArray().first().toObject()
                                  .value(QStringLiteral("text")).toString();
        QVERIFY(first.startsWith(QStringLiteral("You are Codex")));

        // The request to run the stand-in was refused; its proposal is the card.
        const QVariantMap card = chat->proposal();
        QCOMPARE(card.value(QStringLiteral("title")).toString(), QStringLiteral("Turn off Wi-Fi power saving"));
        QCOMPARE(card.value(QStringLiteral("state")).toString(), QStringLiteral("offered"));
        QCOMPARE(card.value(QStringLiteral("undoWay")).toString(), QStringLiteral("Undo puts it back as it was"));
        QVERIFY(runner.records().isEmpty());

        chat->doIt();
        QTRY_COMPARE_WITH_TIMEOUT(chat->proposal().value(QStringLiteral("state")).toString(), QStringLiteral("done"), 15000);
        QCOMPARE(runner.records().size(), 1);
        QVERIFY(chat->proposal().value(QStringLiteral("canUndo")).toBool());
        QTRY_COMPARE_WITH_TIMEOUT(lastText(*chat, QStringLiteral("codex")), QStringLiteral("It's done. Do the drops stop now?"), 10000);
        QCOMPARE(chat->notice(), QStringLiteral("Done: Turn off Wi-Fi power saving."));
    }

    // The stand-in itself, run where Codex runs, puts its proposal on the
    // card too.
    void theStandInProposes()
    {
        PretendFixture fixture;
        QTemporaryDir store;
        ChangeRunner runner(store.path());
        auto chat = chatFor(fixture, QStringLiteral("codex.json"), &runner);
        chat->begin(QStringLiteral("show me hidden files, through the stand-in"));
        QTRY_COMPARE_WITH_TIMEOUT(chat->proposal().value(QStringLiteral("title")).toString(), QStringLiteral("Show hidden files"), 10000);
        QVERIFY(lastText(*chat, QStringLiteral("codex")).contains(QStringLiteral("Proposed.")));
        // Not now leaves it unmade, and Codex hears so with the next question.
        chat->notNow();
        QVERIFY(chat->proposal().isEmpty());
        QVERIFY(runner.records().isEmpty());
    }

    // Every other request to act is refused at once.
    void everyOtherRequestIsRefused()
    {
        PretendFixture fixture;
        auto chat = chatFor(fixture, QStringLiteral("codex.json"), nullptr);
        chat->begin(QStringLiteral("delete my documents, I forgot a password"));
        QTRY_COMPARE_WITH_TIMEOUT(lastText(*chat, QStringLiteral("codex")), QStringLiteral("That was refused."), 10000);
        QVERIFY(chat->proposal().isEmpty());
        bool refused = false;
        for (const QJsonObject &m : fixture.received()) {
            const QString option = m.value(QStringLiteral("result")).toObject().value(QStringLiteral("outcome"))
                                       .toObject().value(QStringLiteral("optionId")).toString();
            QVERIFY(option != QLatin1String("yes"));
            refused = refused || option == QLatin1String("no");
        }
        QVERIFY(refused);
    }

    // The fence: Codex sees its own folder and nothing else of the person's,
    // no drive beside the system, and no key or token from the environment.
    void codexRunsInTheFence()
    {
        if (!fenceWorks())
            QSKIP("bubblewrap can't make a fence here.");
        PretendFixture fixture;
        const QString home = QDir::homePath();
        QFile secret(QDir(home).filePath(QStringLiteral("private.txt")));
        QVERIFY(secret.open(QIODevice::WriteOnly));
        secret.close();
        QFile outside(QDir(home).filePath(QStringLiteral("../../outside.txt")));
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("outside the home folder");
        outside.close();
        qputenv("LEAKY_TOKEN", "sekrit-token");
        qputenv("SOMEONES_API_KEY", "sekrit-key");
        auto chat = chatFor(fixture, QStringLiteral("codex.json"), nullptr);
        chat->begin(QStringLiteral("look around"));
        QTRY_COMPARE_WITH_TIMEOUT(chat->phase(), QStringLiteral("ready"), 10000);
        qunsetenv("LEAKY_TOKEN");
        qunsetenv("SOMEONES_API_KEY");
        const QString seen = lastText(*chat, QStringLiteral("codex"));
        QFile::remove(secret.fileName());
        QFile::remove(outside.fileName());
        QVERIFY2(seen.contains(QStringLiteral("ENV:")), qPrintable(seen));
        QVERIFY2(!seen.contains(QStringLiteral("private.txt")), qPrintable(seen));
        QVERIFY2(!seen.contains(QStringLiteral("outside the home folder")), qPrintable(seen));
        QVERIFY2(!seen.contains(QStringLiteral("sekrit")), qPrintable(seen));
        QVERIFY2(seen.contains(QStringLiteral("SPLIT_ROCK_PRETEND_HOME=")), qPrintable(seen));
        // Its own folder is there.
        const QString listed = seen.section(QStringLiteral("HOME:\n"), 1).section(QStringLiteral("MNT:"), 0, 0);
        QVERIFY2(listed.contains(QDir(home).relativeFilePath(fixture.state.path())), qPrintable(listed));
    }

    // What of the environment goes inside, by name.
    void theEnvironmentKeepsOnlyWhatItNeeds()
    {
        QProcessEnvironment base;
        for (const char *name : {"PATH", "HOME", "LANG", "LC_TIME", "TZ", "HTTPS_PROXY", "XDG_CONFIG_HOME",
                                 "GITHUB_TOKEN", "OPENAI_API_KEY", "CODEX_HOME", "AWS_SECRET_ACCESS_KEY",
                                 "DBUS_SESSION_BUS_ADDRESS", "WAYLAND_DISPLAY", "SSH_AUTH_SOCK", "PASSWORD"})
            base.insert(QLatin1String(name), QStringLiteral("x"));
        const QStringList kept = ask::fencedEnvironment(base).keys();
        QCOMPARE(QSet<QString>(kept.begin(), kept.end()),
                 (QSet<QString>{QStringLiteral("PATH"), QStringLiteral("HOME"), QStringLiteral("LANG"),
                                QStringLiteral("LC_TIME"), QStringLiteral("TZ"), QStringLiteral("HTTPS_PROXY"),
                                QStringLiteral("XDG_CONFIG_HOME")}));
    }

    // Signing in goes through Codex's own page: a way that needs a key or a
    // terminal is never offered, and the page opens only while signing in.
    void signsInThroughItsOwnPage()
    {
        PretendFixture fixture;
        auto chat = chatFor(fixture, QStringLiteral("codex-sign-in.json"), nullptr);
        QSignalSpy pages(chat.get(), &Chat::openPage);
        // From Settings: no question, only the sign-in, started at once.
        chat->begin(QString());
        QTRY_COMPARE_WITH_TIMEOUT(chat->phase(), QStringLiteral("signedIn"), 10000);
        QCOMPARE(chat->signInName(), QStringLiteral("Login with ChatGPT"));
        QCOMPARE(pages.size(), 1);
        QCOMPARE(pages.first().at(0).toString(), QStringLiteral("https://sign-in.example/start"));
        QCOMPARE(fixture.received(QStringLiteral("authenticate")).first().value(QStringLiteral("params")).toObject()
                     .value(QStringLiteral("methodId")).toString(), QStringLiteral("chatgpt"));
        QVERIFY(fixture.assistant(QStringLiteral("codex-sign-in.json")).signedIn());
        // A question then goes straight on.
        chat->ask(QStringLiteral("Say hello after signing in"));
        QTRY_COMPARE_WITH_TIMEOUT(lastText(*chat, QStringLiteral("codex")), QStringLiteral("Hello after signing in."), 10000);
        // Signing out forgets it.
        QVERIFY(fixture.assistant(QStringLiteral("codex-sign-in.json")).signOut());
        QVERIFY(!fixture.assistant(QStringLiteral("codex-sign-in.json")).signedIn());
    }

    // From Search, signed out: the question waits for the person's tap on
    // Sign in.
    void aQuestionWaitsForTheSignIn()
    {
        PretendFixture fixture;
        auto chat = chatFor(fixture, QStringLiteral("codex-sign-in.json"), nullptr);
        chat->begin(QStringLiteral("Say hello after signing in"));
        QTRY_COMPARE_WITH_TIMEOUT(chat->phase(), QStringLiteral("signIn"), 10000);
        QVERIFY(fixture.received(QStringLiteral("authenticate")).isEmpty());
        chat->signIn();
        QTRY_COMPARE_WITH_TIMEOUT(lastText(*chat, QStringLiteral("codex")), QStringLiteral("Hello after signing in."), 10000);
    }

    // Keep as a note keeps the conversation, under the first question; the
    // window closing forgets it, and what Codex wrote of it.
    void keepsAndForgets()
    {
        PretendFixture fixture;
        Kept kept;
        auto chat = chatFor(fixture, QStringLiteral("codex.json"), nullptr, &kept);
        chat->begin(QStringLiteral("my wifi keeps dropping"));
        QTRY_COMPARE_WITH_TIMEOUT(chat->phase(), QStringLiteral("ready"), 10000);
        QVERIFY(chat->keep());
        QVERIFY(chat->kept());
        QCOMPARE(kept.title, QStringLiteral("my wifi keeps dropping"));
        QVERIFY(kept.text.startsWith(QStringLiteral("You: my wifi keeps dropping\n\nCodex: Your card's power saving is on.")));
        QVERIFY(!chat->keep());
        QVERIFY(chat->keptOpens());
        QVERIFY(chat->openKept());
        QCOMPARE(kept.opened, QStringLiteral("note-1"));
        QVERIFY(QFileInfo::exists(fixture.home() + QStringLiteral("/transcripts")));
        chat->end();
        QVERIFY(!QFileInfo::exists(fixture.home() + QStringLiteral("/transcripts")));
    }

    // Where Codex isn't installed, the window says so and starts nothing.
    void withoutCodexItSaysSo()
    {
        Chat chat(codex::codex(QString(), QDir::homePath() + QStringLiteral("/codex-missing")), nullptr, nullptr, {});
        chat.begin(QStringLiteral("my wifi keeps dropping"));
        QCOMPARE(chat.phase(), QStringLiteral("unavailable"));
        QCOMPARE(chat.problem(), QStringLiteral("Codex isn't installed on this computer."));
    }
};

QTEST_GUILESS_MAIN(TestCodex)
#include "test_codex.moc"
