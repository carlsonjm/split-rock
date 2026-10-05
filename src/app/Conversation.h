// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Client.h"
#include "app/AnswerText.h"
#include "app/Assistants.h"
#include "app/Change.h"
#include "guide/GuideLibrary.h"

#include <QObject>
#include <QSet>
#include <QTemporaryDir>
#include <QVariantList>

#include <memory>

class QProcess;
class QTimer;

namespace splitrock {

class ChangeRunner;
class Notebook;

// One conversation with one assistant, as Genie's window shows it.
//
// Nothing here is written to disk. The assistant works in an empty folder made
// for it and removed with it, and what it writes about the conversation in its
// own folder is removed whenever it stops.
class Conversation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList assistants READ assistants NOTIFY assistantsChanged)
    Q_PROPERTY(bool choosing READ choosing NOTIFY choosingChanged)
    Q_PROPERTY(QString assistantId READ assistantId NOTIFY assistantChanged)
    Q_PROPERTY(QString assistantName READ assistantName NOTIFY assistantChanged)
    Q_PROPERTY(QString assistantCost READ assistantCost NOTIFY assistantChanged)
    // choose, starting, signIn, signingIn, ready, working or problem.
    Q_PROPERTY(QString phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(QVariantList signInChoices READ signInChoices NOTIFY signInChanged)
    Q_PROPERTY(QVariantList notOffered READ notOffered NOTIFY signInChanged)
    Q_PROPERTY(QString signingInWith READ signingInWith NOTIFY signInChanged)
    Q_PROPERTY(bool signingInWithoutTerminal READ signingInWithoutTerminal NOTIFY signInChanged)
    Q_PROPERTY(QString signInPage READ signInPage NOTIFY signInChanged)
    Q_PROPERTY(QString question READ question NOTIFY answerChanged)
    Q_PROPERTY(QString headline READ headline NOTIFY answerChanged)
    // A line under the headline, such as a tour's opening words.
    Q_PROPERTY(QString lead READ lead NOTIFY answerChanged)
    // The tour that follows the one shown, or empty.
    Q_PROPERTY(QString nextTour READ nextTour NOTIFY answerChanged)
    Q_PROPERTY(QStringList steps READ steps NOTIFY answerChanged)
    Q_PROPERTY(QStringList more READ more NOTIFY answerChanged)
    Q_PROPERTY(QVariantMap card READ card NOTIFY cardChanged)
    Q_PROPERTY(bool cardOpen READ cardOpen NOTIFY cardChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY noticeChanged)
    Q_PROPERTY(QString problemDetails READ problemDetails NOTIFY noticeChanged)
    Q_PROPERTY(QStringList suggestions READ suggestions NOTIFY assistantChanged)
    // The questions asked earlier in this conversation, each with its answer:
    // question, headline, steps and more. The one being answered is not here.
    Q_PROPERTY(QVariantList turns READ turns NOTIFY turnsChanged)
    // What the assistant asked to change in this conversation and was refused,
    // oldest first: heading and question.
    Q_PROPERTY(QVariantList refusedChanges READ refusedChanges NOTIFY turnsChanged)
    // Keep this was used on this answer.
    Q_PROPERTY(bool kept READ kept NOTIFY answerChanged)
    // There is a notebook to keep answers in.
    Q_PROPERTY(bool canKeep READ canKeep NOTIFY notebookChanged)
    // The fixed problems in This computer.md, and the kept notes: title,
    // topic, path and date, newest first.
    Q_PROPERTY(QVariantList fixedProblems READ fixedProblems NOTIFY notebookChanged)
    Q_PROPERTY(QVariantList deviceFixes READ deviceFixes NOTIFY notebookChanged)
    Q_PROPERTY(QStringList deviceStillOpen READ deviceStillOpen NOTIFY notebookChanged)
    // What the assistant offers to remember about the person, until the
    // person answers; and whether That fixed it was used on this answer.
    Q_PROPERTY(QString remember READ remember NOTIFY rememberChanged)
    Q_PROPERTY(bool fixed READ fixed NOTIFY answerChanged)
    Q_PROPERTY(QVariantList keptNotes READ keptNotes NOTIFY notebookChanged)
    // About me.md, as written.
    Q_PROPERTY(QString aboutMe READ aboutMe NOTIFY notebookChanged)
    Q_PROPERTY(bool isGuide READ isGuide NOTIFY assistantChanged)
    // The changes Split Rock made, newest first: id, title, date, status,
    // undoWay and canUndo.
    Q_PROPERTY(QVariantList changes READ changes NOTIFY changesChanged)
    // A change is being made or undone.
    Q_PROPERTY(bool changing READ changing NOTIFY changesChanged)

public:
    explicit Conversation(QList<Assistant> assistants, QObject *parent = nullptr);
    ~Conversation() override;

    // What the assistant is told with the first question of a conversation.
    static QString framing();

    // Offers the guide in the box, first in the list, when it has entries.
    void setGuide(const GuideLibrary &library, const GuideContext &context);
    bool isGuide() const { return m_current.guide; }

    QVariantList assistants() const;
    bool choosing() const { return m_choosing; }
    QString assistantId() const { return m_current.id; }
    QString assistantName() const { return m_current.name; }
    QString assistantCost() const { return m_current.cost; }
    QString phase() const { return m_phase; }
    QVariantList signInChoices() const;
    QVariantList notOffered() const;
    QString signingInWith() const { return m_signingIn.method.name; }
    bool signingInWithoutTerminal() const { return m_signingIn.how == SignInChoice::How::WithoutTerminal; }
    QString signInPage() const { return m_signInPage; }
    QString question() const { return m_question; }
    // A question is waiting for the assistant to be ready.
    bool waiting() const { return !m_waiting.isEmpty(); }
    QString headline() const { return m_answer.headline; }
    QString lead() const { return m_lead; }
    QString nextTour() const;
    QStringList steps() const { return m_answer.steps; }
    QStringList more() const { return m_answer.more; }
    QVariantMap card() const { return m_card; }
    bool cardOpen() const { return m_cardOpen; }
    QString notice() const { return m_notice; }
    QString problem() const { return m_problem; }
    QString problemDetails() const { return m_problemDetails; }
    QStringList suggestions() const;
    QVariantList turns() const { return m_turns; }
    QVariantList refusedChanges() const { return m_refusedChanges; }

    Q_INVOKABLE void showAssistants();
    Q_INVOKABLE void hideAssistants();
    Q_INVOKABLE void chooseAssistant(const QString &id);
    // Starts the assistant the person chose last time, if none is chosen yet
    // and it is still offered. True when an assistant is chosen afterwards.
    bool resumeChosen();

    // The notebook this conversation remembers in; none in tests that do not
    // need one. Not owned.
    void setNotebook(Notebook *notebook);
    bool kept() const { return m_kept; }
    bool canKeep() const { return m_notebook != nullptr; }
    QVariantList fixedProblems() const;
    QVariantList deviceFixes() const;
    QStringList deviceStillOpen() const;
    // A problem still open, solved, in the person's own words.
    Q_INVOKABLE bool solveStillOpen(const QString &problem, const QString &fix);
    // A solved problem came back: back under Still open.
    Q_INVOKABLE bool reopenFix(const QString &path);
    QString remember() const { return m_rememberAnswered || m_phase == QLatin1String("working") ? QString() : m_answer.remember; }
    bool fixed() const { return m_fixed; }
    // "Remember that?": yes writes it into About me.md; no lets it go.
    Q_INVOKABLE bool rememberYes();
    Q_INVOKABLE void rememberNo();
    // That fixed it: the problem and what fixed it go into This computer.md.
    Q_INVOKABLE bool markFixed();
    QVariantList keptNotes() const;
    QString aboutMe() const;
    // Keep this: the answer becomes a note in the notebook. False, with why in
    // the notice, when it may not be kept.
    Q_INVOKABLE bool keep();
    // Forget: a kept note goes to the desktop's trash.
    Q_INVOKABLE bool forget(const QString &path);
    Q_INVOKABLE void signIn(const QString &methodId);
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void ask(const QString &text);
    Q_INVOKABLE void stop();
    // Do it: makes the change proposed on the card.
    Q_INVOKABLE void doIt();
    // Undo, from the notebook: puts a change back.
    Q_INVOKABLE bool undoChange(const QString &id);
    QVariantList changes() const;
    bool changing() const;
    ChangeRunner *changeRunner() const { return m_runner; }
    // A change the assistant proposed, shown on the Do it card.
    void propose(const ChangeProposal &proposed);
    Q_INVOKABLE void showMeHow();
    Q_INVOKABLE void notNow();
    Q_INVOKABLE void openCard();
    Q_INVOKABLE void newQuestion();
    // The guide's tour: the first, or the one after the tour shown.
    Q_INVOKABLE void showTour();

Q_SIGNALS:
    // A sign-in page the assistant asked to open, for the person's browser.
    void signInPageReady(const QString &page);
    void assistantsChanged();
    void choosingChanged();
    void assistantChanged();
    void phaseChanged();
    void signInChanged();
    void answerChanged();
    void cardChanged();
    void noticeChanged();
    void turnsChanged();
    void notebookChanged();
    void rememberChanged();
    void changesChanged();

private:
    void startAssistant();
    void stopAssistant();
    void startSession();
    void askFirst();
    void enterSignIn(const acp::RpcError &error);
    void signInWithoutTerminal(const SignInChoice &choice);
    void signInDidNotFinish(const QString &details);
    void stopSignInProcess();
    void send(const QString &prompt, const QString &shown);
    void setPhase(const QString &phase);
    void setNotice(const QString &notice);
    void setProblem(const QString &problem, const QString &details);
    void clearAnswer();
    void answerFromGuide(const QString &question);
    QList<GuideEntry> tours() const;
    void keepTurn();
    // A request the fence and the notebook's rules allow.
    bool mayWrite(const acp::ToolCall &call) const;
    // The answer as text, from the assistant or the guide.
    QString answerText() const;
    void clearTurns();
    QString recentDiagnostics() const;

    QList<Assistant> m_assistants;
    GuideLibrary m_guide;
    GuideContext m_guideContext;
    int m_tour = -1;
    QString m_lead;
    Assistant m_current;
    acp::Client *m_client = nullptr;
    std::unique_ptr<QTemporaryDir> m_workDir;
    QProcess *m_signInProcess = nullptr;
    QTimer *m_signInTimeout = nullptr;
    QString m_signInOutput;

    bool m_choosing = true;
    QString m_phase = QStringLiteral("choose");
    QList<SignInChoice> m_choices;
    SignInChoice m_signingIn;
    QString m_signInPage;
    // The assistant's requests to open a web page, from inside its fence.
    void readOpenRequests();
    QString m_openRequests;
    qint64 m_openRequestsRead = 0;
    QTimer *m_openRequestsTimer = nullptr;
    // The assistant's proposed changes, from the split-rock-change stand-in.
    void readChangeRequests();
    QString m_changeRequests;
    qint64 m_changeRequestsRead = 0;
    QTimer *m_changeRequestsTimer = nullptr;
    ChangeRunner *m_runner = nullptr;
    std::optional<ChangeProposal> m_proposal;
    // How a change went, told to the assistant once it is ready.
    QString m_report;
    QString m_reportShown;
    // Told to the assistant with the next question: what happened since.
    QString m_aside;
    bool m_framed = false;

    QString m_question;
    // A question asked while the assistant was starting or being signed in,
    // sent as soon as it is ready.
    QString m_waiting;
    QString m_said;
    // What came between the last words said and the next: nothing, a tool
    // call, or a look that finished, after which the answer starts afresh.
    enum class SaidBefore { Nothing, Tool, Look };
    SaidBefore m_saidBefore = SaidBefore::Nothing;
    AnswerText m_answer;
    QVariantMap m_card;
    bool m_cardOpen = false;
    QSet<QString> m_refused;
    QSet<QString> m_allowed;
    Notebook *m_notebook = nullptr;
    bool m_kept = false;
    bool m_fixed = false;
    bool m_rememberAnswered = false;
    QVariantList m_turns;
    QVariantList m_refusedChanges;
    QSet<QString> m_unasked;
    QString m_notice;
    QString m_problem;
    QString m_problemDetails;
    QStringList m_diagnostics;
};

} // namespace splitrock
