// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "guide/SkillShelf.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace splitrock::desk {

class Model;

// What the front desk reaches on the computer: the stand-ins Split Rock puts
// in the fence (Fence.cpp), or fakes in a test.
class Tools
{
public:
    virtual ~Tools() = default;
    // Opens a settings skill: its text, then what its look-around read on this
    // computer, as split-rock-skill prints them.
    virtual void openSkill(const QString &id, const std::function<void(const QString &)> &done) = 0;
    // Hands a proposal to Split Rock, as split-rock-change does. False when it
    // couldn't be handed over.
    virtual bool propose(const QJsonObject &proposal) = 0;
};

// What Split Rock tells the front desk as a conversation starts.
struct Context {
    QList<Skill> skills;
    // Split Rock's own upkeep cards, by name, each with what it is for.
    QList<std::pair<QString, QString>> cards;
    // The assistant a bigger change is handed to, by name; empty when none is
    // set up.
    QString handOffTo;
    // What Split Rock knows about the computer: its system, read by the
    // desk itself.
    QString facts;
    // The notebook's memory, for answers.
    QString memory;
};

// The command shapes a skill's Change section names, such as
// "plasma-apply-cursortheme <name>".
QStringList skillCommands(const QString &skillText);

// Genie's card check, which asks no model. Empty when the card may be shown;
// otherwise why not. Its command must be one the skill names, with each name
// as the skill's look-around read it on this computer, and nothing joined on
// or run as administrator.
QString checkCard(const QJsonObject &card, const QString &skillText, const QString &seen);

// An answer that says a change was made, which an answer in words never does.
bool claimsDone(const QString &answer);
// An answer that sends the person to a terminal.
bool sendsToTerminal(const QString &answer);

// The front desk (docs/DECISIONS.md, Genie on this computer): Genie keeps the
// steering, and the model on this computer only sorts each ask, answers in
// words, or fills in a card Genie checks.
//
// 1. Genie's safety check, with no model, answers what must never reach one.
// 2. The model sorts the ask: an answer, a change and the skill or upkeep card
//    that covers it, or an unsafe ask and its kind.
// 3. An answer is written in words, with no tools, and shown as it is written.
// 4. For a skill, Genie opens it, runs its look-around, and the model fills in
//    the Do it card; Genie checks the card, and asks the model once more
//    whether it does what was asked, before the person sees it.
// 5. An upkeep card is opened by name; Split Rock's own card does the rest.
// 6. Anything else that changes the computer is handed to a bigger assistant.
// When the model only talks where the ask plainly wants a change, Genie looks
// for the skill or card itself.
class FrontDesk : public QObject
{
    Q_OBJECT

public:
    FrontDesk(Model *model, Tools *tools, QObject *parent = nullptr);

    void setContext(const Context &context) { m_context = context; }
    const Context &context() const { return m_context; }

    // Fills the model's memory with the start of each kind of request, so the
    // first ask is answered sooner.
    void warm();
    void ask(const QString &question);
    // How a change or an upkeep card Genie proposed went, from Split Rock.
    // kind is change, card, refused or update; ok whether it worked.
    void report(const QJsonObject &what);
    void cancel();
    bool busy() const { return m_busy; }

    // The sorting step's instructions and reply shape, for tests and the trial.
    QString sortingWords() const;
    QJsonObject sortingShape() const;

Q_SIGNALS:
    // Words for the person, as they come.
    void said(const QString &text);
    // The ask is done; handOff when it needs the bigger assistant.
    void finished(bool handOff);
    // How the ask was handled, for the log and tests: gate:<kind>,
    // answer, skill:<id>, card:<name>, handoff, unsafe:<kind>.
    void routed(const QString &how);

private:
    void say(const QString &text);
    void finish(bool handOff);
    void route(const QString &question);
    void answer(const QString &question);
    void fillCard(const QString &question, const Skill &skill);
    void openCard(const QString &question, const QString &card, const QString &query);
    void handOff(const QString &why);
    QString answerWords() const;
    QJsonArray withHistory(const QString &system, const QString &question, int turns) const;
    // The upkeep card an ask plainly needs, by its words, or empty.
    QString cardFor(const QString &question) const;
    void remember(const QString &question, const QString &answer);

    Model *m_model;
    Tools *m_tools;
    Context m_context;
    SkillShelf m_shelf;
    bool m_busy = false;
    QString m_question;
    QString m_said;
    // Earlier asks and answers in this conversation, oldest first.
    QList<std::pair<QString, QString>> m_history;
    int m_generation = 0;
};

} // namespace splitrock::desk
