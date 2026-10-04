// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Client.h"
#include "app/AnswerText.h"
#include "app/Assistants.h"

#include <QObject>
#include <QSet>
#include <QTemporaryDir>
#include <QVariantList>

#include <memory>

class QProcess;
class QTimer;

namespace splitrock {

// One conversation with one assistant, as Genie's window shows it.
//
// Nothing here is written to disk. The assistant works in an empty folder made
// for it and removed with it, and what it writes about the conversation in its
// own folder is removed whenever it stops.
class Conversation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList assistants READ assistants CONSTANT)
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
    Q_PROPERTY(QStringList steps READ steps NOTIFY answerChanged)
    Q_PROPERTY(QStringList more READ more NOTIFY answerChanged)
    Q_PROPERTY(QVariantMap card READ card NOTIFY cardChanged)
    Q_PROPERTY(bool cardOpen READ cardOpen NOTIFY cardChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY noticeChanged)
    Q_PROPERTY(QString problemDetails READ problemDetails NOTIFY noticeChanged)
    Q_PROPERTY(QStringList suggestions READ suggestions CONSTANT)
    // The questions asked earlier in this conversation, each with its answer:
    // question, headline, steps and more. The one being answered is not here.
    Q_PROPERTY(QVariantList turns READ turns NOTIFY turnsChanged)
    // What the assistant asked to change in this conversation and was refused,
    // oldest first: heading and question.
    Q_PROPERTY(QVariantList refusedChanges READ refusedChanges NOTIFY turnsChanged)

public:
    explicit Conversation(QList<Assistant> assistants, QObject *parent = nullptr);
    ~Conversation() override;

    // What the assistant is told with the first question of a conversation.
    static QString framing();

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
    QString headline() const { return m_answer.headline; }
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
    Q_INVOKABLE void signIn(const QString &methodId);
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void ask(const QString &text);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void showMeHow();
    Q_INVOKABLE void notNow();
    Q_INVOKABLE void openCard();
    Q_INVOKABLE void newQuestion();

Q_SIGNALS:
    void choosingChanged();
    void assistantChanged();
    void phaseChanged();
    void signInChanged();
    void answerChanged();
    void cardChanged();
    void noticeChanged();
    void turnsChanged();

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
    void clearTurns();
    QString recentDiagnostics() const;

    QList<Assistant> m_assistants;
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
    bool m_framed = false;

    QString m_question;
    QString m_said;
    AnswerText m_answer;
    QVariantMap m_card;
    bool m_cardOpen = false;
    QSet<QString> m_refused;
    QVariantList m_turns;
    QVariantList m_refusedChanges;
    QSet<QString> m_unasked;
    QString m_notice;
    QString m_problem;
    QString m_problemDetails;
    QStringList m_diagnostics;
};

} // namespace splitrock
