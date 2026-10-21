// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Client.h"
#include "app/Change.h"
#include "codex/Agent.h"

#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace splitrock {
class ChangeRunner;
class Notebook;
} // namespace splitrock

namespace splitrock::codex {

// One conversation with Codex, in its own window (docs/ASK.md, Codex): the
// bigger questions Ask hands on, with the person's own ChatGPT plan.
//
// Codex runs in the fence (ask/Fence.h), in a folder of its own, and changes
// nothing itself. Every request it makes to act is refused; a change it
// proposes with the split-rock-change stand-in goes on a Do it card, and is
// made only when the person taps Do it, by Split Rock, outside the fence.
// What it says is always shown as Codex's, never mixed with Ask's.
// Nothing is kept when the window closes unless the person taps Keep.
class Chat : public QObject
{
    Q_OBJECT
    // idle, starting, signIn, signingIn, signedIn, ready, working, problem
    // or unavailable.
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    // Each turn: who ("you" or "codex") and text.
    Q_PROPERTY(QVariantList turns READ turns NOTIFY turnsChanged)
    // The Do it card, or empty: title, what, why, undoWay, and state
    // (offered, making, done, failed, undone, undoFailed).
    Q_PROPERTY(QVariantMap proposal READ proposal NOTIFY proposalChanged)
    // A line under the conversation: what just happened.
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY phaseChanged)
    // How the person signs in, in Codex's own words: "Login with ChatGPT".
    Q_PROPERTY(QString signInName READ signInName NOTIFY phaseChanged)
    // The page the sign-in opened, for when the browser didn't come up.
    Q_PROPERTY(QString signInPage READ signInPage NOTIFY phaseChanged)
    Q_PROPERTY(bool kept READ kept NOTIFY keptChanged)
    // The kept note can be opened in Notes, which outlives this window.
    Q_PROPERTY(bool keptOpens READ keptOpens NOTIFY keptChanged)

public:
    // What the window does with what Codex hands it: opens one of the
    // Helpers, by name, keeps the conversation as a note, saying in note
    // which note when Notes kept it, and opens that note in Notes.
    struct Hands {
        std::function<bool(const QString &card, const QString &query)> openCard;
        std::function<bool(const QString &title, const QString &text, QString *note, QString *error)> keep;
        std::function<bool(const QString &note)> openNote;
    };

    Chat(const Agent &agent, ChangeRunner *runner, Notebook *notebook, Hands hands, QObject *parent = nullptr);
    ~Chat() override;

    void setHands(Hands hands) { m_hands = std::move(hands); }

    QString phase() const { return m_phase; }
    QVariantList turns() const { return m_turns; }
    QVariantMap proposal() const { return m_proposal; }
    QString notice() const { return m_notice; }
    QString problem() const { return m_problem; }
    QString signInName() const { return m_signIn ? m_signIn->name : QString(); }
    QString signInPage() const { return m_signInPage; }
    bool kept() const { return m_kept; }
    bool keptOpens() const { return m_kept && !m_keptNote.isEmpty() && m_hands.openNote; }

    // Starts Codex, with a first question or, when empty, only to sign in.
    Q_INVOKABLE void begin(const QString &question);
    Q_INVOKABLE void ask(const QString &text);
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void doIt();
    Q_INVOKABLE void notNow();
    Q_INVOKABLE void undo();
    // Keeps the conversation as a note: in Notes where Gooseberry is
    // installed, otherwise in the notebook.
    Q_INVOKABLE bool keep();
    Q_INVOKABLE bool openKept();
    // Stops Codex and forgets the conversation.
    Q_INVOKABLE void end();

    // What Codex is told before the first question.
    static QString framing();

    // The fence program; empty runs the agent unfenced, which only the
    // pretend assistant may.
    void setFence(const QString &program) { m_fence = program; }
    // How long a sign-in may take before it is given up.
    void setSignInLimit(int ms) { m_signInLimit = ms; }

Q_SIGNALS:
    void phaseChanged();
    void turnsChanged();
    void proposalChanged();
    void noticeChanged();
    void keptChanged();
    // The sign-in asked for its page: the window opens it in the browser.
    void openPage(const QString &page);

private:
    void start();
    void startSession();
    void enterSignIn();
    void send(const QString &prompt);
    void say(const QString &text);
    void setPhase(const QString &phase, const QString &problem = {});
    void setNotice(const QString &notice);
    void readRequests();
    void propose(const ChangeProposal &proposed);
    void report(const QString &text);
    bool writeStandIns();

    Agent m_agent;
    ChangeRunner *m_runner;
    Notebook *m_notebook;
    Hands m_hands;
    QPointer<acp::Client> m_client;
    std::unique_ptr<QTemporaryDir> m_work;
    QString m_phase = QStringLiteral("idle");
    QString m_problem;
    QString m_notice;
    QVariantList m_turns;
    QVariantMap m_proposal;
    std::optional<ChangeProposal> m_change;
    QString m_changeId;
    std::optional<acp::AuthMethod> m_signIn;
    QString m_signInPage;
    QString m_waiting;
    QString m_report;
    // Told with the next prompt: what happened since the last answer.
    QString m_aside;
    bool m_framed = false;
    bool m_kept = false;
    QString m_keptNote;
    bool m_onlySignIn = false;
    bool m_answering = false;
    bool m_freshTurn = false;
    QSet<QString> m_refused;
    QString m_requests;
    qint64 m_openRead = 0;
    qint64 m_changeRead = 0;
    QTimer m_poll;
    QTimer m_signInTimer;
    int m_signInLimit = 10 * 60 * 1000;
    QString m_fence;
};

} // namespace splitrock::codex
