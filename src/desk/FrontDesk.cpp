// SPDX-License-Identifier: GPL-2.0-or-later
#include "desk/FrontDesk.h"

#include "desk/Gate.h"
#include "desk/Model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>

#include <limits>

namespace splitrock::desk {

namespace {

// Each step's longest reply, in tokens.
constexpr int SortingTokens = 80;
constexpr int CardTokens = 400;
constexpr int FitTokens = 20;
constexpr int AnswerTokens = 700;
// The earlier asks and answers the model sees with each answer.
constexpr int HistoryTurns = 3;
// The slots in the model's memory: sorting and its yes-or-no checks, and
// answers and cards.
constexpr int SortingSlot = 0;
constexpr int AnswerSlot = 1;

QJsonObject cardShape()
{
    const QJsonObject text{{QStringLiteral("type"), QStringLiteral("string")}};
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("title"), text},
                     {QStringLiteral("what"), text},
                     {QStringLiteral("why"), text},
                     {QStringLiteral("command"), text},
                     {QStringLiteral("admin"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}},
                     {QStringLiteral("undo"), text},
                     {QStringLiteral("check"), text}}},
        {QStringLiteral("required"),
         QJsonArray{QStringLiteral("title"), QStringLiteral("what"), QStringLiteral("why"), QStringLiteral("command"),
                    QStringLiteral("admin"), QStringLiteral("undo"), QStringLiteral("check")}},
    };
}

QJsonObject yesNoShape()
{
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), QJsonObject{{QStringLiteral("fits"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}},
        {QStringLiteral("required"), QJsonArray{QStringLiteral("fits")}},
    };
}

QJsonObject message(const QString &role, const QString &content)
{
    return QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("content"), content}};
}

// Words that ask for something to be done rather than explained.
bool asksForAChange(const QString &question)
{
    static const QRegularExpression doIt(QStringLiteral(
        "^\\s*(please\\s+)?(make|turn|set|change|switch|put|use|give|let|install|open|free|clean|update|"
        "can you|could you|would you|will you|i want|i'd like|i would like|help me)\\b|"
        "\\b(make it|make the|turn (it|on|off)|switch (it|to|back)|change (it|the|to)|put (it|the) back|"
        "can (it|you|i have)|could (it|you)|please)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return doIt.match(question).hasMatch();
}

// Words that look back at something that changed or stopped working: "undo
// that", "it worked yesterday", "why did this stop working?".
bool looksBack(const QString &question)
{
    static const QRegularExpression back(QStringLiteral(
        "\\bundo (that|it|this|what you did|the last|my last|the change)\\b|\\bput (it|that|things|everything) back\\b|"
        "\\bchange (it|that) back\\b|\\bwhat('s| has)? changed\\b|\\bsomething (has )?changed\\b|"
        "\\b(was|were) (working|fine|ok|okay) (yesterday|before|earlier|this morning|last week)\\b|"
        "\\bworked (fine )?(yesterday|before|earlier|last week)\\b|\\bstopped working\\b|\\bstop working\\b|"
        "\\b(isn't|is not|doesn't|does not|won't|no longer) work(s|ing)?\\b.{0,24}\\b(any ?more|since|after)\\b|"
        "\\bbroke (after|since)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return back.match(question).hasMatch();
}

QString cardTitle(const QString &card)
{
    if (card == QLatin1String("update"))
        return QStringLiteral("Update the computer");
    if (card == QLatin1String("space"))
        return QStringLiteral("Free up space");
    if (card == QLatin1String("install"))
        return QStringLiteral("Install an app");
    if (card == QLatin1String("restore"))
        return QStringLiteral("Go back to a restore point");
    if (card == QLatin1String("health"))
        return QStringLiteral("Check the computer's health");
    if (card == QLatin1String("community"))
        return QStringLiteral("Update community apps");
    if (card == QLatin1String("changes"))
        return QStringLiteral("What changed");
    return card;
}

} // namespace

QStringList skillCommands(const QString &skillText, const QString &section)
{
    const QRegularExpression part(QStringLiteral("^## %1\\s*$(.*?)(?=^## |\\z)").arg(QRegularExpression::escape(section)),
                                  QRegularExpression::MultilineOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression span(QStringLiteral("`([^`]+)`"));
    QStringList shapes;
    const QString text = part.match(skillText).captured(1);
    for (auto it = span.globalMatch(text); it.hasNext();) {
        const QString shape = it.next().captured(1).simplified();
        if (shape.contains(QLatin1Char('<')))
            shapes.append(shape);
    }
    return shapes;
}

namespace {

// The words of a command a skill names, or of one the model wrote, and the
// names a shape leaves open in it ("<app>"), in order; false when the command
// is not that shape.
bool fitsShape(const QStringList &words, const QString &shape, QStringList *names = nullptr,
               QStringList *holes = nullptr)
{
    const QStringList parts = QProcess::splitCommand(shape);
    if (parts.size() != words.size())
        return false;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (parts.at(i).startsWith(QLatin1Char('<')) && parts.at(i).endsWith(QLatin1Char('>'))) {
            if (names)
                names->append(words.at(i));
            if (holes)
                holes->append(parts.at(i));
        } else if (parts.at(i) != words.at(i)) {
            return false;
        }
    }
    return true;
}

// Words joined into a command line, each quoted where a shell would read it
// otherwise.
QString shellJoin(const QStringList &words)
{
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z0-9_@%+=:,./-]+$"));
    QStringList quoted;
    for (const QString &word : words) {
        quoted.append(plain.match(word).hasMatch() ? word
                                                   : QLatin1Char('\'') + QString(word).replace(QLatin1Char('\''), QStringLiteral("'\\''")) + QLatin1Char('\''));
    }
    return quoted.join(QLatin1Char(' '));
}

// The shape among shapes that leaves open the same names, in order, filled
// with names; empty when there is none. Of several, the one closest to the
// command's own words: "systemctl --user is-active <service>" after
// "systemctl --user restart <service>".
QString filled(const QStringList &shapes, const QStringList &holes, const QStringList &names, const QStringList &command)
{
    QStringList best;
    int bestScore = std::numeric_limits<int>::min();
    for (const QString &shape : shapes) {
        QStringList parts = QProcess::splitCommand(shape);
        QStringList open;
        int score = 0;
        for (const QString &part : parts) {
            if (part.startsWith(QLatin1Char('<')) && part.endsWith(QLatin1Char('>')))
                open.append(part);
            else
                score += command.contains(part) ? 1 : -1;
        }
        if (open != holes || score <= bestScore)
            continue;
        qsizetype n = 0;
        for (QString &part : parts) {
            if (part.startsWith(QLatin1Char('<')) && part.endsWith(QLatin1Char('>')))
                part = names.at(n++);
        }
        best = parts;
        bestScore = score;
    }
    return best.isEmpty() ? QString() : shellJoin(best);
}

} // namespace

QString checkCard(const QJsonObject &card, const QString &skillText, const QString &seen)
{
    const QString command = card.value(QStringLiteral("command")).toString().trimmed();
    if (command.isEmpty())
        return QStringLiteral("no command");
    static const QRegularExpression joined(QStringLiteral("[;&|`$<>(){}\\n\\\\]"));
    static const QRegularExpression rights(QStringLiteral("\\b(sudo|pkexec|doas|su)\\b"));
    if (joined.match(command).hasMatch() || rights.match(command).hasMatch())
        return QStringLiteral("joins commands or asks for administrator rights");
    const QStringList words = QProcess::splitCommand(command);
    if (words.isEmpty())
        return QStringLiteral("unreadable command");
    for (const QString &shape : skillCommands(skillText)) {
        QStringList names;
        if (!fitsShape(words, shape, &names))
            continue;
        for (const QString &name : names) {
            const QRegularExpression listed(QStringLiteral("(?<![\\w.-])%1(?![\\w.-])").arg(QRegularExpression::escape(name)),
                                            QRegularExpression::CaseInsensitiveOption);
            if (!listed.match(seen).hasMatch())
                return QStringLiteral("%1 isn't on this computer").arg(name);
        }
        return {};
    }
    return QStringLiteral("not a command this skill names");
}

QJsonObject withSkillsOwnUndo(const QJsonObject &card, const QString &skillText)
{
    const QStringList words = QProcess::splitCommand(card.value(QStringLiteral("command")).toString().trimmed());
    QStringList names;
    QStringList holes;
    bool matched = false;
    for (const QString &shape : skillCommands(skillText)) {
        names.clear();
        holes.clear();
        if (fitsShape(words, shape, &names, &holes)) {
            matched = true;
            break;
        }
    }
    QJsonObject out = card;
    if (!matched)
        return out;
    for (const char *section : {"Undo", "Check"}) {
        const QStringList shapes = skillCommands(skillText, QLatin1String(section));
        if (shapes.isEmpty())
            continue;
        const QString key = QString::fromLatin1(section).toLower();
        out.insert(key, filled(shapes, holes, names, words));
    }
    // A change with nothing to put back, such as starting a service again,
    // says so in the skill; whatever undo the model wrote is not run.
    static const QRegularExpression nothing(QStringLiteral("^## Undo\\s*$[^#]*leave \"undo\" empty"),
                                            QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    if (nothing.match(skillText).hasMatch())
        out.insert(QStringLiteral("undo"), QString());
    return out;
}

bool claimsDone(const QString &answer)
{
    static const QRegularExpression did(QStringLiteral(
        "\\b(has been|have been|is now|are now|should now be|i('ve| have)) (changed|set|opened|open|switched|applied|"
        "installed|removed|turned|made|fixed|updated|deleted|connected|enabled|disabled)\\b|\\bi (changed|set|opened|switched|installed|applied|"
        "fixed|updated|removed|turned|connected|enabled|disabled)\\b|\\bi'?ll (apply|change|set|switch|install|turn|open)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return did.match(answer).hasMatch();
}

bool sendsToTerminal(const QString &answer)
{
    static const QRegularExpression terminal(QStringLiteral("\\bterminal\\b|\\bsudo\\b|\\bapt(-get)?\\b|\\bdpkg\\b|\\bkonsole\\b"),
                                             QRegularExpression::CaseInsensitiveOption);
    return terminal.match(answer).hasMatch();
}

FrontDesk::FrontDesk(Model *model, Tools *tools, QObject *parent)
    : QObject(parent)
    , m_model(model)
    , m_tools(tools)
{
}

QString FrontDesk::sortingWords() const
{
    QString skills;
    for (const Skill &skill : m_context.skills) {
        skills += QStringLiteral("- %1: %2").arg(skill.id, skill.title);
        if (!skill.also.isEmpty())
            skills += QStringLiteral(" (for example: %1)").arg(skill.also.join(QStringLiteral("; ")));
        skills += QLatin1Char('\n');
    }
    QString cards;
    for (const auto &[name, what] : m_context.cards)
        cards += QStringLiteral("- %1: %2\n").arg(name, what);
    return QStringLiteral(
        "You sort messages for a desktop helper on a Linux computer. Decide how to handle the person's last message.\n"
        "kind is \"answer\" when they want to know or understand something, or are just chatting; \"change\" when "
        "they want something set, switched, installed, removed, freed, updated or done on the computer, including "
        "when they only describe a problem a change would fix, such as \"it's too loud\" or \"the screen is too "
        "bright at night\"; \"unsafe\" when helping could hurt them or someone else: scams, payment or bank codes, "
        "passwords, remote control by others, deleting the system, pirated software, watching someone secretly, "
        "switching protection off.\n"
        "target is the skill or card below that covers a change, or \"none\".\n"
        "Settings skills:\n%1"
        "Upkeep cards:\n%2"
        "worry is the kind of unsafe ask (%3), or \"none\".\n"
        "query is, only for the install card, the app or kind of app wanted, such as \"pdf editor\"; otherwise empty.")
        .arg(skills, cards, Gate::kinds().join(QStringLiteral(", ")));
}

QJsonObject FrontDesk::sortingShape() const
{
    QJsonArray targets;
    for (const Skill &skill : m_context.skills)
        targets.append(skill.id);
    for (const auto &card : m_context.cards)
        targets.append(card.first);
    targets.append(QStringLiteral("none"));
    QJsonArray worries;
    for (const QString &kind : Gate::kinds())
        worries.append(kind);
    worries.append(QStringLiteral("none"));
    const auto oneOf = [](const QJsonArray &values) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("enum"), values}};
    };
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("kind"), oneOf({QStringLiteral("answer"), QStringLiteral("change"), QStringLiteral("unsafe")})},
                     {QStringLiteral("target"), oneOf(targets)},
                     {QStringLiteral("worry"), oneOf(worries)},
                     {QStringLiteral("query"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
        {QStringLiteral("required"),
         QJsonArray{QStringLiteral("kind"), QStringLiteral("target"), QStringLiteral("worry"), QStringLiteral("query")}},
    };
}

QString FrontDesk::answerWords() const
{
    QString words = QStringLiteral(
        "You are the helper on this computer, which runs Linux with the KDE Plasma desktop. The person has used "
        "Windows for years and is new to Linux. Answer in plain words, with no jargon, and name the Windows "
        "equivalent when it helps. Start with one short sentence, then give at most five short numbered steps when "
        "steps help. Be brief and warm.\n"
        "In this reply you cannot look at or change the computer, and nothing happens because of what you write, so "
        "never say that something was changed, opened, checked or fixed, and never promise to do it. Never tell the "
        "person to open a terminal or type a command; point to System Settings, Discover (the app store) or the "
        "app's own menus instead. When the person wants something changed that you can't do here, say plainly "
        "that you can't do that one from here. Never ask for a password, a card number, bank details or a code "
        "sent by text or email. If you don't know, say so; you can't see the news, the weather or the internet.\n");
    if (!m_context.facts.isEmpty())
        words += m_context.facts + QLatin1Char('\n');
    if (!m_context.memory.isEmpty())
        words += QStringLiteral("What the person's notebook remembers:\n%1\n").arg(m_context.memory.left(2000));
    return words;
}

QJsonArray FrontDesk::withHistory(const QString &system, const QString &question, int turns) const
{
    QJsonArray messages{message(QStringLiteral("system"), system)};
    for (qsizetype i = std::max<qsizetype>(0, m_history.size() - turns); i < m_history.size(); ++i) {
        messages.append(message(QStringLiteral("user"), m_history.at(i).first));
        messages.append(message(QStringLiteral("assistant"), m_history.at(i).second));
    }
    messages.append(message(QStringLiteral("user"), question));
    return messages;
}

void FrontDesk::warm()
{
    if (m_model->busy())
        return;
    // Only the opening words are read; nothing is shown.
    m_model->complete(withHistory(sortingWords(), QStringLiteral("hello"), 0), sortingShape(), SortingSlot, 1, {},
                      [this](const QString &, const QString &) {
                          if (m_busy || m_model->busy())
                              return;
                          m_model->complete(withHistory(answerWords(), QStringLiteral("hello"), 0), {}, AnswerSlot, 1,
                                            [](const QString &) {}, [](const QString &, const QString &) {});
                      });
}

void FrontDesk::say(const QString &text)
{
    if (text.isEmpty())
        return;
    m_said += text;
    Q_EMIT said(text);
}

void FrontDesk::finish(bool handOff)
{
    if (!m_busy)
        return;
    m_busy = false;
    remember(m_question, m_said);
    Q_EMIT finished(handOff);
}

void FrontDesk::remember(const QString &question, const QString &answer)
{
    if (question.isEmpty())
        return;
    m_history.append({question, answer.trimmed().isEmpty() ? QStringLiteral("(no answer)") : answer.trimmed()});
    while (m_history.size() > HistoryTurns)
        m_history.removeFirst();
}

void FrontDesk::cancel()
{
    ++m_generation;
    m_model->cancel();
    finish(false);
}

void FrontDesk::ask(const QString &question)
{
    if (m_busy) {
        cancel();
    }
    m_busy = true;
    m_question = question;
    m_said.clear();
    ++m_generation;
    if (m_shelf.skills().size() != m_context.skills.size())
        m_shelf.setSkills(m_context.skills);

    if (const std::optional<QString> kind = Gate::check(question)) {
        Q_EMIT routed(QStringLiteral("gate:") + *kind);
        say(Gate::answer(*kind));
        finish(false);
        return;
    }
    // The model may still be warming up; the sorting waits for it.
    if (m_model->busy())
        m_model->cancel();
    route(question);
}

void FrontDesk::route(const QString &question)
{
    const int generation = m_generation;
    m_model->complete(withHistory(sortingWords(), question, 1), sortingShape(), SortingSlot, SortingTokens, {},
                      [this, generation, question](const QString &text, const QString &error) {
        if (generation != m_generation)
            return;
        QJsonObject sorted;
        if (error.isEmpty())
            sorted = QJsonDocument::fromJson(text.toUtf8()).object();
        const QString kind = sorted.value(QStringLiteral("kind")).toString(QStringLiteral("answer"));
        const QString target = sorted.value(QStringLiteral("target")).toString(QStringLiteral("none"));
        const QString worry = sorted.value(QStringLiteral("worry")).toString(QStringLiteral("none"));
        const QString query = sorted.value(QStringLiteral("query")).toString();

        if (kind == QLatin1String("unsafe")) {
            Q_EMIT routed(QStringLiteral("unsafe:") + worry);
            const QString fixed = Gate::answer(worry);
            say(fixed.isEmpty() ? QStringLiteral("That one could put you or this computer at risk, so I'd rather not "
                                                 "help with it here. If I've misread you, tell me a little more about "
                                                 "what you're trying to do.")
                                : fixed);
            finish(false);
            return;
        }
        // Looking back at what changed goes to the What changed card, unless
        // the model found a settings skill that makes the change asked for.
        const bool skillChange = kind == QLatin1String("change") && m_shelf.skill(target);
        if (!skillChange && looksBack(question) && offers(QStringLiteral("changes"))) {
            openCard(question, QStringLiteral("changes"), {});
            return;
        }
        for (const auto &card : m_context.cards) {
            if (card.first == target && kind == QLatin1String("change")) {
                openCard(question, target, query);
                return;
            }
        }
        if (const Skill *skill = m_shelf.skill(target); skill && kind == QLatin1String("change")) {
            fillCard(question, *skill);
            return;
        }
        // The model only talked, or found nothing, where the ask plainly wants
        // something done: Genie looks for the skill or card itself.
        if (kind == QLatin1String("change") || asksForAChange(question)) {
            if (const Skill *skill = m_shelf.find(question)) {
                fillCard(question, *skill);
                return;
            }
            const QString card = cardFor(question);
            if (!card.isEmpty()) {
                openCard(question, card, query);
                return;
            }
        }
        if (kind == QLatin1String("change")) {
            handOff(QStringLiteral("no skill or card covers it"));
            return;
        }
        answer(question);
    });
}

bool FrontDesk::offers(const QString &card) const
{
    return std::any_of(m_context.cards.cbegin(), m_context.cards.cend(), [&card](const auto &c) { return c.first == card; });
}

QString FrontDesk::cardFor(const QString &question) const
{
    static const QList<std::pair<QString, QRegularExpression>> words{
        {QStringLiteral("space"), QRegularExpression(QStringLiteral(
             "\\b(disk|drive|storage|space)\\b.{0,40}\\b(full|almost full|running out|low|free up|eating)|"
             "free up (some )?(space|room|storage)|out of (space|storage|room)|clean ?up"), QRegularExpression::CaseInsensitiveOption)},
        {QStringLiteral("update"), QRegularExpression(QStringLiteral(
             "\\bupdate (the |my |this )?(computer|system|everything|tablet|laptop)\\b|\\bupdates?\\b.{0,30}\\b(install|run|do|waiting|available)\\b|"
             "\\b(install|run|do) (the )?updates?\\b"), QRegularExpression::CaseInsensitiveOption)},
        {QStringLiteral("health"), QRegularExpression(QStringLiteral(
             "\\b(so |really |very )?slow\\b|health check|check (the |my |this )?(computer|system|tablet|laptop)|"
             "something (is |seems )?wrong with (the |my |this )?(computer|system)"), QRegularExpression::CaseInsensitiveOption)},
        {QStringLiteral("restore"), QRegularExpression(QStringLiteral(
             "restore point|go back to (how it was|yesterday|before)|undo (the |an )?update|roll ?back"), QRegularExpression::CaseInsensitiveOption)},
    };
    for (const auto &[card, pattern] : words) {
        const bool offered = std::any_of(m_context.cards.cbegin(), m_context.cards.cend(),
                                         [&card](const auto &c) { return c.first == card; });
        if (offered && pattern.match(question).hasMatch())
            return card;
    }
    return {};
}

void FrontDesk::answer(const QString &question)
{
    Q_EMIT routed(QStringLiteral("answer"));
    const int generation = m_generation;
    m_model->complete(withHistory(answerWords(), question, HistoryTurns), {}, AnswerSlot, AnswerTokens,
                      [this, generation](const QString &chunk) {
                          if (generation == m_generation)
                              say(chunk);
                      },
                      [this, generation](const QString &text, const QString &error) {
        if (generation != m_generation)
            return;
        if (!error.isEmpty() && text.isEmpty()) {
            say(QStringLiteral("I couldn't answer that just now. Try asking again."));
            finish(false);
            return;
        }
        // The words are already on screen; what they got wrong is put right
        // beneath them.
        if (claimsDone(text))
            say(QStringLiteral("\n\nTo be clear, nothing on this computer has been changed. Ask me to do it, and if "
                               "I can, I'll show you a card first."));
        else if (sendsToTerminal(text))
            say(QStringLiteral("\n\nYou won't need a terminal for this: ask me, and I'll do what I can with a card "
                               "you approve."));
        finish(false);
    });
}

void FrontDesk::openCard(const QString &question, const QString &card, const QString &query)
{
    Q_EMIT routed(QStringLiteral("card:") + card);
    QJsonObject proposal{{QStringLiteral("title"), cardTitle(card)}, {QStringLiteral("card"), card}};
    if (card == QLatin1String("install")) {
        const QString wanted = query.trimmed().isEmpty() ? question : query.trimmed();
        proposal.insert(QStringLiteral("query"), wanted.left(80));
    }
    if (!m_tools->propose(proposal)) {
        say(QStringLiteral("I couldn't open that card just now. Try asking again."));
        finish(false);
        return;
    }
    say(QStringLiteral("I'll open the %1 card. It looks first, and nothing changes until you choose.")
            .arg(cardTitle(card)));
    finish(false);
}

void FrontDesk::fillCard(const QString &question, const Skill &skill)
{
    Q_EMIT routed(QStringLiteral("skill:") + skill.id);
    const int generation = m_generation;
    m_tools->openSkill(skill.id, [this, generation, question, skill](const QString &opened) {
        if (generation != m_generation)
            return;
        static const QString seenHeading = QStringLiteral("\n## On this computer now\n");
        const qsizetype at = opened.indexOf(seenHeading);
        const QString skillText = at < 0 ? opened : opened.left(at);
        const QString seen = at < 0 ? QString() : opened.mid(at + seenHeading.size()).trimmed();
        const QString system = answerWords() + QStringLiteral(
            "\nThe person's ask needs a change. Here is the settings skill for it and what it reads on this computer "
            "now.\n\n%1\n\n## On this computer now\n\n%2\n\nFill in the Do it card for the person's ask: title is a "
            "short instruction, what and why are plain words, command is exactly one command the skill's Change "
            "section names, using names exactly as listed above. Never put sudo in it. When the skill says Split "
            "Rock writes the undo, leave undo empty. The card is shown to the person; nothing changes until they tap "
            "Do it.").arg(skillText, seen);
        m_model->complete(withHistory(system, question, 1), cardShape(), AnswerSlot, CardTokens, {},
                          [this, generation, question, skillText, seen](const QString &text, const QString &error) {
            if (generation != m_generation)
                return;
            const QJsonObject card = QJsonDocument::fromJson(text.toUtf8()).object();
            const QString why = error.isEmpty() ? checkCard(card, skillText, seen) : error;
            if (!why.isEmpty()) {
                handOff(QStringLiteral("the card failed Genie's check: ") + why);
                return;
            }
            // One more question for the model, apart from the card: does it do
            // what the person asked?
            const QString fit = QStringLiteral(
                "Someone asked a desktop helper: \"%1\"\nThe helper proposes this change: %2 (%3).\nDoes this change "
                "do what they asked for, and not something else? fits is true only if it plainly does.")
                .arg(question, card.value(QStringLiteral("title")).toString(), card.value(QStringLiteral("what")).toString());
            m_model->complete(QJsonArray{message(QStringLiteral("user"), fit)}, yesNoShape(), SortingSlot, FitTokens, {},
                              [this, generation, card, skillText](const QString &text, const QString &error) {
                if (generation != m_generation)
                    return;
                if (!error.isEmpty() || !QJsonDocument::fromJson(text.toUtf8()).object().value(QStringLiteral("fits")).toBool()) {
                    handOff(QStringLiteral("the card doesn't do what was asked"));
                    return;
                }
                QJsonObject proposal = withSkillsOwnUndo(card, skillText);
                if (proposal.value(QStringLiteral("undo")).toString().trimmed().isEmpty())
                    proposal.remove(QStringLiteral("undo"));
                if (proposal.value(QStringLiteral("check")).toString().trimmed().isEmpty())
                    proposal.remove(QStringLiteral("check"));
                if (!m_tools->propose(proposal)) {
                    say(QStringLiteral("I couldn't show the card just now. Try asking again."));
                    finish(false);
                    return;
                }
                say(QStringLiteral("Here's a card for that: %1. Nothing changes until you tap Do it.")
                        .arg(card.value(QStringLiteral("what")).toString().trimmed()));
                finish(false);
            });
        });
    });
}

void FrontDesk::handOff(const QString &why)
{
    Q_EMIT routed(QStringLiteral("handoff: ") + why);
    if (m_context.handOffTo.isEmpty()) {
        say(QStringLiteral("That's a bigger change than I can make on this computer by myself. With a ChatGPT "
                           "account, Codex can make changes like this one; you can add it from the list of "
                           "assistants."));
        finish(false);
        return;
    }
    say(QStringLiteral("That's a bigger change than I can make on this computer by myself. %1 can do it: tap Ask %1, "
                       "and I'll pass your question over.").arg(m_context.handOffTo));
    finish(true);
}

void FrontDesk::report(const QJsonObject &what)
{
    m_busy = true;
    m_question.clear();
    m_said.clear();
    ++m_generation;
    const QString kind = what.value(QStringLiteral("kind")).toString();
    const QString title = what.value(QStringLiteral("title")).toString();
    const bool ok = what.value(QStringLiteral("ok")).toBool();
    if (kind == QLatin1String("change")) {
        say(ok ? QStringLiteral("Done: %1. If it isn't what you wanted, you can put it back from Changes.").arg(title)
               : QStringLiteral("That didn't work, and nothing else was changed."));
        if (!ok && !m_context.handOffTo.isEmpty())
            say(QStringLiteral(" %1 can look into why: tap Ask %1.").arg(m_context.handOffTo));
        finish(!ok && !m_context.handOffTo.isEmpty());
        return;
    }
    if (kind == QLatin1String("card") || kind == QLatin1String("update")) {
        say(ok ? QStringLiteral("It's open. It looks first, and nothing changes until you choose.")
               : QStringLiteral("Another card is busy. Once it finishes, ask me again."));
        finish(false);
        return;
    }
    if (kind == QLatin1String("chore")) {
        say(ok ? QStringLiteral("Finished: %1.").arg(title) : QStringLiteral("That didn't finish: %1.").arg(title));
        finish(false);
        return;
    }
    if (kind == QLatin1String("refused")) {
        say(QStringLiteral("I couldn't find that on this computer, so I didn't show a card."));
        finish(false);
        return;
    }
    finish(false);
}

} // namespace splitrock::desk
