// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "guide/SkillShelf.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

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

// The command shapes a section of a skill names, such as
// "plasma-apply-cursortheme <name>" under Change.
QStringList skillCommands(const QString &skillText, const QString &section = QStringLiteral("Change"));

// The desk's card check, which asks no model. Empty when the card may be shown;
// otherwise why not. Its command must be one the skill names, with each name
// as the skill's look-around read it on this computer, and nothing joined on
// or run as administrator.
QString checkCard(const QJsonObject &card, const QString &skillText, const QString &seen);

// The card with the undo and check its skill names for the command, filled
// with the command's own names, where the skill names them; the model's own
// are not used then. A skill that says there is nothing to put back leaves
// the undo empty.
QJsonObject withSkillsOwnUndo(const QJsonObject &card, const QString &skillText);

// The Helper an ask plainly needs, by its words alone, among those offered,
// or empty. It needs no model, so it answers before the model is downloaded.
QString helperFor(const QString &question, const QList<std::pair<QString, QString>> &cards);

// The Helper that is itself the answer to a plain "how do I" ask, such as
// "how do I update my computer" or "how do I uninstall an app", among those
// offered, or empty. Steps in words would only send the person past it.
QString helperIsTheHow(const QString &question, const QList<std::pair<QString, QString>> &cards);

// An answer's lines as they are shown. The first line is shown as it is
// written. After it, a line goes up once it is whole, and is left out when it
// ends on what can't be done here, promises a card, or names a taskbar or a
// Search bar at the top, which this desktop doesn't have; the steps that stay
// are numbered again.
class AnswerLines
{
public:
    // What to show of the words that came in.
    QString add(const QString &chunk);
    // What to show of the rest, once the answer is done.
    QString finish();

private:
    QString line(const QString &text);
    bool m_pastFirst = false;
    QString m_first;
    QString m_open;
    int m_gap = 0;
    int m_step = 0;
};

// The desk's own words for an ask with nothing to answer yet, such as "help"
// or a row of keys, or nothing when there is something to answer.
std::optional<QString> unclear(const QString &question);

// Text without emoji, which an answer never carries.
QString withoutEmoji(const QString &text);

// An answer that says what it can't see: that the computer is up to date, or
// fine.
bool claimsToKnow(const QString &answer);

// A block Split Rock labelled as how something it ran went, rather than the
// person's question: a change, a card it opened, an upkeep card that finished,
// a proposal it refused, or a report with no kind of its own.
bool isReport(const QString &kind);

// An answer that says a change was made, which an answer in words never does.
bool claimsDone(const QString &answer);
// An answer that sends the person to a terminal.
bool sendsToTerminal(const QString &answer);

// The front desk (docs/DECISIONS.md, Ask on this computer): the desk keeps the
// steering, and the model on this computer only sorts each ask, answers in
// words, or fills in a card the desk checks.
//
// 1. The desk's safety check, with no model, answers what must never reach one.
// 2. The model sorts the ask: an answer, a change and the skill or upkeep card
//    that covers it, or an unsafe ask and its kind.
// 3. An answer is written in words, with no tools, and shown as it is written.
// 4. For a skill, the desk opens it, runs its look-around, and the model fills in
//    the Do it card; the desk checks the card, and asks the model once more
//    whether it does what was asked, before the person sees it.
// 5. An upkeep card is opened by name; Split Rock's own card does the rest.
// 6. Anything else that changes the computer is handed to a bigger assistant
//    where one is signed in, and answered in words otherwise. A card the desk
//    can't show is answered in words too.
// When the model only talks where the ask plainly wants a change, the desk looks
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
    // How a change or an upkeep card the desk proposed went, from Split Rock.
    // kind is change, card, refused or update; ok whether it worked.
    void report(const QJsonObject &what);
    void cancel();
    bool busy() const { return m_busy; }
    // Forgets the earlier asks, so the next is asked afresh.
    void forget() { m_history.clear(); }

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
    // An answer in words, with the Helper that fits under it, if any; how is
    // what routed says.
    void answer(const QString &question, const QString &helper = {}, const QString &how = QStringLiteral("answer"));
    void fillCard(const QString &question, const Skill &skill);
    void openCard(const QString &question, const QString &card, const QString &query);
    void handOff(const QString &why);
    QString answerWords() const;
    QJsonArray withHistory(const QString &system, const QString &question, int turns) const;
    // Split Rock offers the upkeep card of that name.
    bool offers(const QString &card) const;
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
