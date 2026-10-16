// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Conversation.h"

#include "app/Appearance.h"
#include "app/ChangeCard.h"
#include "app/ChangeRunner.h"
#include "app/Fence.h"
#include "app/NeverWritten.h"
#include "app/Notebook.h"
#include "app/Product.h"

#include <QDate>
#include <QDebug>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

namespace splitrock {

namespace {

// The skill named in a request to run the split-rock-skill stand-in and
// nothing else, if it is one, perhaps wrapped in a shell's -c string.
QString skillIn(const acp::ToolCall &call)
{
    static const QRegularExpression alone(QStringLiteral("^\\s*split-rock-skill\\s+([a-z0-9-]{1,64})\\s*$"));
    QStringList words;
    const QJsonValue command = call.rawInput.toObject().value(QStringLiteral("command"));
    if (command.isArray()) {
        for (const QJsonValue &word : command.toArray())
            words.append(word.toString());
    } else {
        words = shellWords(call.command());
    }
    for (int depth = 0; depth < 3; ++depth) {
        if (words.size() == 2 && words.first() == QLatin1String("split-rock-skill"))
            return alone.match(words.join(QLatin1Char(' '))).captured(1);
        // A shell given one -c string and nothing after it: bash -lc '...'.
        if (words.size() == 3 && (words.at(1) == QLatin1String("-c") || words.at(1) == QLatin1String("-lc"))
            && QStringList{QStringLiteral("sh"), QStringLiteral("bash"), QStringLiteral("/bin/sh"),
                           QStringLiteral("/bin/bash"), QStringLiteral("/usr/bin/bash")}.contains(words.first())) {
            if (const QRegularExpressionMatch match = alone.match(words.at(2)); match.hasMatch())
                return match.captured(1);
            words = shellWords(words.at(2));
            continue;
        }
        break;
    }
    return {};
}

// The proposal in a request to run the split-rock-change stand-in, if it is
// one.
std::optional<ChangeProposal> proposalIn(const acp::ToolCall &call)
{
    const QLatin1String program(ChangeProposal::Program);
    const QJsonValue command = call.rawInput.toObject().value(QStringLiteral("command"));
    if (command.isArray()) {
        const QJsonArray words = command.toArray();
        for (qsizetype i = 0; i < words.size(); ++i) {
            const QString word = words.at(i).toString();
            if (word == program && i + 1 < words.size())
                return ChangeProposal::fromJson(words.at(i + 1).toString().toUtf8());
            if (word.contains(program))
                return ChangeProposal::fromCommandLine(word);
        }
        return std::nullopt;
    }
    const QString line = call.command();
    if (!line.contains(program))
        return std::nullopt;
    return ChangeProposal::fromCommandLine(line);
}

} // namespace

namespace {

// Split Rock's own settings: which assistant the person chose, and nothing else.
QString chosenFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/split-rockrc");
}

constexpr int SignInLimitMs = 10 * 60 * 1000;
constexpr int DiagnosticLines = 30;

QString clientVersion()
{
    return QStringLiteral(PROJECT_VERSION);
}

} // namespace

Conversation::Conversation(QList<Assistant> assistants, QObject *parent)
    : QObject(parent)
    , m_assistants(std::move(assistants))
{
    // The offer is shown once the answer is complete, so it follows the
    // phase as well as the answer.
    connect(this, &Conversation::answerChanged, this, &Conversation::rememberChanged);
    connect(this, &Conversation::phaseChanged, this, &Conversation::rememberChanged);

    m_runner = new ChangeRunner(ChangeRunner::defaultStore(), this);
    connect(m_runner, &ChangeRunner::made, this, [this](const ChangeRunner::Record &record) {
        const bool ok = record.status == QLatin1String("done");
        if (m_notebook) {
            m_notebook->markChange(record.id, ok ? QStringLiteral("Done") : QStringLiteral("Failed"));
            Q_EMIT notebookChanged();
        }
        m_proposal.reset();
        m_card.clear();
        m_cardOpen = false;
        Q_EMIT cardChanged();
        Q_EMIT changesChanged();
        setNotice(ok ? QStringLiteral("Done: %1.").arg(record.proposal.title)
                     : QStringLiteral("That didn't work: %1. Nothing else was changed.").arg(record.proposal.title));
        QString output = record.output;
        if (output.size() > 3000)
            output = QStringLiteral("...") + output.right(3000);
        const QString checked = record.checked.isEmpty()
            ? QStringLiteral("You gave no check, so nothing was read back from the running desktop. A command that "
                             "succeeds does not prove the change took effect, so don't say it worked: ask the person.")
            : QStringLiteral("Your check, run after the change, read back from the running desktop:\n%1\n"
                             "Say it worked only if this shows the change took effect.").arg(record.checked.right(2000));
        m_report = QStringLiteral("The person tapped Do it, and %1 ran your proposed change \"%2\" as the person. Exit code %3. "
                                  "Output:\n%5\n\n%6\n\nTell the person in plain words how it went. You cannot see their "
                                  "screen or running programs, so when the change is something only they can see, ask them "
                                  "whether it worked. Don't propose the same change again unless it failed and you know why.")
                       .arg(productName(), record.proposal.title)
                       .arg(record.exitCode)
                       .arg(output.isEmpty() ? QStringLiteral("(none)") : output, checked);
        m_reportMeta = {{QStringLiteral("kind"), QStringLiteral("change")}, {QStringLiteral("ok"), ok},
                        {QStringLiteral("title"), record.proposal.title}};
        m_reportShown = ok ? QStringLiteral("Did it: %1").arg(record.proposal.title)
                           : QStringLiteral("It didn't work: %1").arg(record.proposal.title);
        if (m_client && m_phase == QLatin1String("ready"))
            sendReport();
    });
    m_update = new SystemUpdate(this);
    addChore(QStringLiteral("update"), m_update);
    m_space = new SpaceCleanup(this);
    addChore(QStringLiteral("space"), m_space);
    m_install = new AppInstall(this);
    addChore(QStringLiteral("install"), m_install);
    m_restore = new RestorePoints(this);
    addChore(QStringLiteral("restore"), m_restore);
    m_health = new HealthCheck(this);
    addChore(QStringLiteral("health"), m_health);
    m_community = new CommunityApps(this);
    addChore(QStringLiteral("community"), m_community);
    m_recent = new RecentChanges(this);
    m_recent->setRecords([this] { return m_runner->records(); });
    addChore(QStringLiteral("changes"), m_recent);
    connect(m_runner, &ChangeRunner::made, m_recent, &RecentChanges::refreshOwn);
    connect(this, &Conversation::assistantChanged, this, &Conversation::tilesChanged);
    connect(m_update, &SystemUpdate::waitingChanged, this, &Conversation::tilesChanged);
    connect(m_health, &Chore::changed, this, &Conversation::tilesChanged);
    connect(m_runner, &ChangeRunner::undone, this, [this](const ChangeRunner::Record &record) {
        const bool ok = record.status == QLatin1String("undone");
        if (m_notebook) {
            m_notebook->markChange(record.id, ok ? QStringLiteral("Undone %1").arg(QDate::currentDate().toString(Qt::ISODate))
                                                 : QStringLiteral("Undo failed"));
            Q_EMIT notebookChanged();
        }
        Q_EMIT changesChanged();
        m_recent->refreshOwn();
        // Told with the person's next question, so the assistant knows the
        // change it made is gone and how the desktop looks now.
        m_aside = QStringLiteral("Since your last answer the person tapped Undo on \"%1\", and %2.")
                      .arg(record.proposal.title,
                           ok ? QStringLiteral("it was put back") : QStringLiteral("putting it back did not work"));
        if (Appearance::touchedBy(record.proposal.command))
            m_aside += QLatin1Char('\n') + Appearance::current().describe();
        setNotice(ok ? QStringLiteral("Undone: %1.").arg(record.proposal.title)
                     : QStringLiteral("The undo didn't work: %1.").arg(record.proposal.title));
    });
}

QObject *Conversation::systemUpdateObject() const
{
    return m_update;
}

QObject *Conversation::spaceObject() const
{
    return m_space;
}

QObject *Conversation::installObject() const
{
    return m_install;
}

QObject *Conversation::restoreObject() const
{
    return m_restore;
}

QObject *Conversation::recentObject() const
{
    return m_recent;
}

QObject *Conversation::healthObject() const
{
    return m_health;
}

QObject *Conversation::communityObject() const
{
    return m_community;
}

void Conversation::addChore(const QString &name, Chore *chore)
{
    m_chores.append({name, chore});
    connect(chore, &Chore::changed, this, &Conversation::choreChanged);
    // Written down, with how to undo it, before the card changes anything.
    chore->setWriteDown([this](const QString &id, const QString &title, const QString &undo, QString *error) {
        if (!m_notebook)
            return true;
        const bool written = m_notebook->recordChange(id, title, QStringLiteral("Asked for on a card of %1's own").arg(productName()),
                                                      undo, QStringLiteral("Making"), error);
        Q_EMIT notebookChanged();
        return written;
    });
    // How it went: in the notebook, and for the assistant with the next
    // question, or now when it is waiting.
    chore->setReport([this](const QString &id, bool ok, const QString &shown, const QString &report) {
        if (m_notebook && !id.isEmpty()) {
            m_notebook->markChange(id, ok ? QStringLiteral("Done") : QStringLiteral("Failed"));
            Q_EMIT notebookChanged();
        }
        m_report = report;
        m_reportShown = shown;
        m_reportMeta = {{QStringLiteral("kind"), QStringLiteral("chore")}, {QStringLiteral("ok"), ok},
                        {QStringLiteral("title"), shown}};
        if (m_client && m_phase == QLatin1String("ready"))
            sendReport();
    });
}

QString Conversation::chore() const
{
    // The busy one first: it can't be closed, so it is the one shown.
    for (const auto &[name, chore] : m_chores) {
        if (chore->busy() && !chore->stage().isEmpty())
            return name;
    }
    for (const auto &[name, chore] : m_chores) {
        if (!chore->stage().isEmpty())
            return name;
    }
    return QString();
}

bool Conversation::choreBusy() const
{
    return std::any_of(m_chores.cbegin(), m_chores.cend(), [](const auto &c) { return c.second->busy(); });
}

bool Conversation::openChore(const QString &name, const QString &query)
{
    const auto found = std::find_if(m_chores.cbegin(), m_chores.cend(), [&name](const auto &c) { return c.first == name; });
    if (found == m_chores.cend())
        return false;
    // Never two at once: what one changes, the other may be looking at.
    if (m_runner->busy())
        return false;
    for (const auto &[other, chore] : m_chores) {
        if (other != name && chore->busy())
            return false;
    }
    if (found->second->busy())
        return true;
    for (const auto &[other, chore] : m_chores) {
        if (other != name)
            chore->close();
    }
    if (m_cardOpen) {
        m_cardOpen = false;
        Q_EMIT cardChanged();
    }
    if (name == QLatin1String("update"))
        m_update->check();
    else if (name == QLatin1String("space"))
        m_space->check();
    else if (name == QLatin1String("install"))
        query.isEmpty() ? m_install->ask() : m_install->search(query);
    else if (name == QLatin1String("restore"))
        m_restore->check();
    else if (name == QLatin1String("health"))
        m_health->check();
    else if (name == QLatin1String("community"))
        m_community->check();
    else if (name == QLatin1String("changes"))
        m_recent->check();
    return true;
}

Conversation::~Conversation()
{
    stopSignInProcess();
    stopAssistant();
}

QString Conversation::framing()
{
    return QStringLiteral(
        "You are helping through %1, a desktop assistant on a Linux computer with the KDE Plasma desktop. "
        "The person has used Windows for years and is new to Linux. Answer in plain words, with no jargon, "
        "and name the Windows equivalent when it helps. "
        "Start with one short sentence, then give at most five short numbered steps when steps help. "
        "You run in a sandbox and cannot change this computer yourself; you may look. To change a setting, a "
        "file or the system, propose it: run split-rock-change with one JSON argument, {\"title\": a short "
        "instruction such as \"Set the fan profile to Quiet\", \"what\": what will change, in plain words, "
        "\"why\": why, in plain words, \"command\": the shell command that makes the change, which runs as the "
        "person and never as the administrator (never put sudo, pkexec or any other way to the administrator in it), "
        "\"undo\": a command that "
        "puts it back, when there is one, applied live the same way, \"check\": a command that reads the live state "
        "back after the change, such as a qdbus6 call to the program the setting belongs to, \"files\": the files "
        "in the home folder the command changes}. Your sandbox cannot reach the running desktop, so %1 runs the "
        "check for you and shows you what it read; reading a settings file back does not show the desktop "
        "changed. Prefer commands that apply a setting live, such as plasma-apply-colorscheme or "
        "plasma-apply-lookandfeel, and after kwriteconfig6 tell the program to reload; change the desktop's look "
        "only with those two, never by editing kdeglobals, using names installed on this computer. The "
        "person sees it on a card and decides; %1 makes the change outside your sandbox and tells you how it went. "
        "For upkeep, %1 has cards of its own, which look first, take restore points and show progress: propose "
        "{\"title\": a short instruction, \"card\": one of update (update the whole computer), space (free up "
        "space), install (install an app, with \"query\": what the person needs, such as \"pdf editor\"), restore "
        "(go back to a restore point), health (a health check), community (update apps from the AUR) or changes (what "
        "changed on this computer lately, each with its way back: for \"undo that\", \"something changed\" or \"why did "
        "this stop working?\")} instead of "
        "a command for those. A change only the administrator can make, other than those cards, is one %1 can't "
        "make: say so in plain words and offer no card. "
        "Propose one change at a time, with a short, specific command that finishes on its own; to open a window, "
        "start it with kstart so the command returns at once. Before proposing, "
        "check the names on this computer rather than from memory, because Plasma 5 and 6 differ: for example "
        "kcmshell6 --list for the settings pages, or kreadconfig6 for a setting's current value. "
        "Your sandbox cannot see the person's running programs, windows or screen, so never say whether a "
        "window is open or what it shows; ask the person instead. "
        "The one place you may write yourself is "
        "the person's notebook, following the rules in its ASSISTANT.md. "
        "Never ask for a password, a card number, bank details or a code sent by text or email. "
        "If you are not sure, say you don't know.").arg(productName());
}

QVariantList Conversation::assistants() const
{
    QVariantList list;
    for (const Assistant &a : m_assistants) {
        list.append(QVariantMap{{QStringLiteral("id"), a.id},
                                {QStringLiteral("name"), a.name},
                                {QStringLiteral("cost"), a.cost},
                                {QStringLiteral("unavailable"), a.unavailable}});
    }
    return list;
}

QVariantList Conversation::signInChoices() const
{
    QVariantList list;
    for (const SignInChoice &choice : m_choices) {
        if (choice.how == SignInChoice::How::NotOffered)
            continue;
        list.append(QVariantMap{{QStringLiteral("id"), choice.method.id},
                                {QStringLiteral("name"), choice.method.name},
                                {QStringLiteral("description"), choice.method.description},
                                {QStringLiteral("withoutTerminal"), choice.how == SignInChoice::How::WithoutTerminal}});
    }
    return list;
}

QVariantList Conversation::notOffered() const
{
    QVariantList list;
    for (const SignInChoice &choice : m_choices) {
        if (choice.how != SignInChoice::How::NotOffered)
            continue;
        list.append(QVariantMap{{QStringLiteral("name"), choice.method.name},
                                {QStringLiteral("whyNot"), choice.whyNot}});
    }
    return list;
}

void Conversation::setGuide(const GuideLibrary &library, const GuideContext &context)
{
    m_guide = library;
    m_guideContext = context;
    m_assistants.erase(std::remove_if(m_assistants.begin(), m_assistants.end(),
                                      [](const Assistant &a) { return a.guide; }),
                       m_assistants.end());
    // Genie on this computer answers from the guide itself, so the guide is a
    // choice of its own only where Genie on this computer can't run.
    const bool localRuns = std::any_of(m_assistants.cbegin(), m_assistants.cend(),
                                       [](const Assistant &a) { return a.local && a.unavailable.isEmpty(); });
    if (!localRuns && !m_guide.entries(m_guideContext, GuideEntry::Kind::Guide).isEmpty()) {
        Assistant guide;
        guide.id = QStringLiteral("guide");
        guide.name = QStringLiteral("%1's guide").arg(productName());
        guide.cost = QStringLiteral("Free, with no account: known problems and their fixes, written ahead of time.");
        guide.guide = true;
        m_assistants.prepend(guide);
    }
    Q_EMIT assistantsChanged();
}

QStringList Conversation::suggestions() const
{
    if (m_current.guide) {
        QStringList titles;
        if (!tours().isEmpty())
            titles.append(QStringLiteral("Show me around"));
        QList<GuideEntry> entries = m_guide.entries(m_guideContext, GuideEntry::Kind::Guide);
        // A desktop's own problems first, as for the tours.
        std::stable_sort(entries.begin(), entries.end(), [](const GuideEntry &a, const GuideEntry &b) {
            return a.source != QLatin1String("plasma") && b.source == QLatin1String("plasma");
        });
        for (const GuideEntry &entry : entries) {
            if (titles.size() == 3)
                break;
            titles.append(entry.title);
        }
        return titles;
    }
    return {QStringLiteral("How do I connect to Wi-Fi?"),
            QStringLiteral("Make the writing bigger"),
            QStringLiteral("A window says my computer is infected")};
}

QVariantList Conversation::tiles() const
{
    QVariantList tiles;
    const auto add = [&tiles](const QString &text, const QString &card, const QString &question, const QString &noticed,
                              const QString &app = {}) {
        for (const QVariant &tile : std::as_const(tiles)) {
            if (tile.toMap().value(QStringLiteral("text")) == text)
                return;
        }
        tiles.append(QVariantMap{{QStringLiteral("text"), text},
                                 {QStringLiteral("card"), card},
                                 {QStringLiteral("question"), question},
                                 {QStringLiteral("noticed"), noticed},
                                 {QStringLiteral("app"), app}});
    };
    const auto hasCard = [this](const QString &name) {
        return std::any_of(m_chores.cbegin(), m_chores.cend(), [&name](const auto &c) { return c.first == name; });
    };
    // The guide answers only from its entries, so its tiles are its own.
    if (m_current.guide) {
        for (const QString &title : suggestions())
            add(title, {}, title, {});
        return tiles;
    }

    // What Genie noticed: updates waiting first, then what the health check
    // found, problems before what's worth a look.
    const int waiting = m_update ? m_update->waiting() : -1;
    if (waiting > 0 && hasCard(QStringLiteral("update")))
        add(waiting == 1 ? QStringLiteral("1 update is waiting") : QStringLiteral("%1 updates are waiting").arg(waiting),
            QStringLiteral("update"), {}, QStringLiteral("warn"));
    if (m_health) {
        for (const HealthCheck::Item &item : m_health->items()) {
            if (tiles.size() == 3)
                break;
            // Waiting updates already say it.
            if (item.id == QLatin1String("updates") && waiting > 0)
                continue;
            const QString text = HealthCheck::tile(item);
            if (text.isEmpty())
                continue;
            // A restart is offered on the Health check card, behind the
            // desktop's own prompt.
            if (item.action == QLatin1String("restart") && hasCard(QStringLiteral("health")))
                add(text, QStringLiteral("health"), {}, item.state);
            else if (!item.action.isEmpty() && item.action != QLatin1String("ask") && hasCard(item.action))
                add(text, item.action, {}, item.state);
            else if (!item.question.isEmpty())
                add(text, {}, item.question, item.state);
        }
    }

    // Then what it can do on this computer: its cards, taking turns with
    // quick things that only open an app the computer already has.
    if (m_health && m_health->lastChecked().isEmpty() && hasCard(QStringLiteral("health")))
        add(QStringLiteral("Give this computer a health check"), QStringLiteral("health"), {}, {});
    QList<std::pair<QString, QString>> offers;
    if (hasCard(QStringLiteral("install")))
        offers.append({QStringLiteral("Install a new app"), QStringLiteral("install")});
    if (hasCard(QStringLiteral("space")))
        offers.append({QStringLiteral("Make room on the disk"), QStringLiteral("space")});
    QList<std::pair<QString, QString>> quick;
    for (const auto &[text, app] : quickApps()) {
        if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation, app + QStringLiteral(".desktop")).isEmpty())
            quick.append({text, app});
    }
    for (qsizetype i = 0; i < std::max(offers.size(), quick.size()); ++i) {
        if (i < offers.size())
            add(offers.at(i).first, offers.at(i).second, {}, {});
        if (i < quick.size())
            add(quick.at(i).first, {}, {}, {}, quick.at(i).second);
    }
    for (const Skill &skill : m_skills.skills()) {
        if (tiles.size() >= 3)
            break;
        if (!skill.also.isEmpty())
            add(skill.also.first(), {}, skill.also.first(), {});
    }
    for (const QString &question : suggestions())
        add(question, {}, question, {});
    while (tiles.size() > 3)
        tiles.removeLast();
    return tiles;
}

const QList<std::pair<QString, QString>> &Conversation::quickApps()
{
    static const QList<std::pair<QString, QString>> apps{
        {QStringLiteral("Take a screenshot"), QStringLiteral("org.kde.spectacle")},
        {QStringLiteral("Find an emoji"), QStringLiteral("org.kde.plasma.emojier")},
        {QStringLiteral("See what's using the computer"), QStringLiteral("org.kde.plasma-systemmonitor")},
    };
    return apps;
}

void Conversation::openTile(const QVariantMap &tile)
{
    // Opening an app changes nothing, so it needs no card.
    const QString app = tile.value(QStringLiteral("app")).toString();
    if (!app.isEmpty()) {
        const auto known = std::find_if(quickApps().cbegin(), quickApps().cend(),
                                        [&app](const auto &quick) { return quick.second == app; });
        if (known == quickApps().cend())
            return;
        QStringList program = m_launcher;
        if (program.isEmpty())
            program = QStandardPaths::findExecutable(QStringLiteral("kstart")).isEmpty()
                ? QStringList{QStringLiteral("gtk-launch")}
                : QStringList{QStringLiteral("kstart"), QStringLiteral("--application")};
        program << app;
        QProcess::startDetached(program.takeFirst(), program);
        return;
    }
    const QString card = tile.value(QStringLiteral("card")).toString();
    if (!card.isEmpty()) {
        openChore(card);
        return;
    }
    const QString question = tile.value(QStringLiteral("question")).toString();
    if (!question.isEmpty())
        ask(question);
}

void Conversation::showAssistants()
{
    if (m_phase == QLatin1String("working") || m_phase == QLatin1String("signingIn"))
        return;
    m_choosing = true;
    Q_EMIT choosingChanged();
}

void Conversation::hideAssistants()
{
    if (m_current.id.isEmpty())
        return;
    m_choosing = false;
    Q_EMIT choosingChanged();
}

void Conversation::chooseAssistant(const QString &id, bool remember)
{
    const auto it = std::find_if(m_assistants.cbegin(), m_assistants.cend(),
                                 [&id](const Assistant &a) { return a.id == id; });
    if (it == m_assistants.cend() || !it->unavailable.isEmpty())
        return;
    stopSignInProcess();
    stopAssistant();
    m_current = *it;
    // Remembered, so that the next time Split Rock starts, from its window or
    // from Search, it starts with the same assistant. Only the choice is kept;
    // the conversation is not.
    if (remember)
        QSettings(chosenFile(), QSettings::IniFormat).setValue(QStringLiteral("Assistant/chosen"), id);
    m_choosing = false;
    Q_EMIT assistantChanged();
    Q_EMIT choosingChanged();
    clearAnswer();
    clearTurns();
    if (m_current.guide) {
        // The guide starts nothing: it is ready at once.
        setNotice({});
        setProblem({}, {});
        setPhase(QStringLiteral("ready"));
        return;
    }
    if (m_current.local && m_local) {
        setNotice({});
        setProblem({}, {});
        if (m_local->choice().id.isEmpty())
            m_local->survey();
        if (!m_local->isReady()) {
            // The model comes first; the person decides to download it.
            setPhase(QStringLiteral("download"));
            return;
        }
    }
    startAssistant();
}

void Conversation::setLocalModel(LocalModel *model)
{
    m_local = model;
    if (m_local) {
        connect(m_local, &LocalModel::changed, this, [this] {
            if (m_current.local && m_phase == QLatin1String("download") && m_local->isReady())
                startAssistant();
        });
    }
    Q_EMIT assistantsChanged();
}

QObject *Conversation::localModelObject() const
{
    return m_local;
}

const Assistant *Conversation::handOffTarget() const
{
    for (const Assistant &a : m_assistants) {
        if (!a.guide && !a.local && a.unavailable.isEmpty())
            return &a;
    }
    return nullptr;
}

void Conversation::handOff()
{
    const Assistant *target = handOffTarget();
    if (!target || m_handOff.isEmpty() || m_phase == QLatin1String("working"))
        return;
    const QString question = m_question;
    chooseAssistant(target->id, false);
    if (!question.isEmpty())
        ask(question);
}

QJsonObject Conversation::framingMeta() const
{
    QJsonArray skills;
    for (const Skill &skill : m_skills.skills()) {
        skills.append(QJsonObject{{QStringLiteral("id"), skill.id},
                                  {QStringLiteral("title"), skill.title},
                                  {QStringLiteral("also"), QJsonArray::fromStringList(skill.also)}});
    }
    QJsonArray cards;
    const QList<std::pair<QString, QString>> upkeep{
        {QStringLiteral("update"), QStringLiteral("update the whole computer, or check whether an update worked")},
        {QStringLiteral("space"), QStringLiteral("free up space when the disk is full or nearly full")},
        {QStringLiteral("install"), QStringLiteral("install an app or find one for a job, or remove (uninstall) an app")},
        {QStringLiteral("restore"), QStringLiteral("go back to a restore point, as things were before")},
        {QStringLiteral("health"), QStringLiteral("a health check, when the computer is slow, hot or something seems wrong")},
        {QStringLiteral("community"), QStringLiteral("update apps from the AUR, the community's own apps")},
        {QStringLiteral("changes"), QStringLiteral("what changed on this computer lately, with Undo: for \"undo that\", "
                                                   "\"put it back\", \"something changed\", \"it worked yesterday\" or "
                                                   "\"why did this stop working?\"")},
    };
    for (const auto &[name, what] : upkeep) {
        if (std::any_of(m_chores.cbegin(), m_chores.cend(), [&name](const auto &c) { return c.first == name; }))
            cards.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("what"), what}});
    }
    const Assistant *target = handOffTarget();
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("framing")},
                       {QStringLiteral("skills"), m_skills.isEmpty() || m_skillRequests.isEmpty() ? QJsonArray() : skills},
                       {QStringLiteral("cards"), m_changeRequests.isEmpty() ? QJsonArray() : cards},
                       {QStringLiteral("handOffTo"), target ? target->name : QString()}};
}

void Conversation::sendReport()
{
    const QJsonObject meta = std::exchange(m_reportMeta, QJsonObject());
    send(std::exchange(m_report, QString()), std::exchange(m_reportShown, QString()),
         meta.isEmpty() ? QJsonObject{{QStringLiteral("kind"), QStringLiteral("report")}} : meta);
}

bool Conversation::resumeChosen()
{
    if (m_current.id.isEmpty())
        chooseAssistant(QSettings(chosenFile(), QSettings::IniFormat).value(QStringLiteral("Assistant/chosen")).toString());
    return !m_current.id.isEmpty();
}

void Conversation::startAssistant()
{
    m_choices.clear();
    m_signingIn = {};
    m_signInPage.clear();
    m_framed = false;
    m_diagnostics.clear();
    setNotice({});
    setProblem({}, {});
    Q_EMIT signInChanged();
    setPhase(QStringLiteral("starting"));

    // Anything left from an earlier run that ended without tidying up.
    removeConversationLeftovers(m_current);
    QDir().mkpath(m_current.home);
    m_workDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/split-rock-XXXXXX"));

    m_client = new acp::Client(this);
    acp::Client *client = m_client;
    connect(client, &acp::Client::diagnostic, this, [this](const QString &line) {
        m_diagnostics.append(line);
        while (m_diagnostics.size() > DiagnosticLines)
            m_diagnostics.removeFirst();
    });
    connect(client, &acp::Client::stopped, this, [this](const QString &reason, bool expected) {
        if (expected)
            return;
        setProblem(QStringLiteral("%1 stopped.").arg(m_current.name), reason + QLatin1Char('\n') + recentDiagnostics());
    });
    connect(client, &acp::Client::messageChunk, this, [this](const QString &text) {
        // What the assistant says before it looks something up is it working
        // aloud; the answer is what it says after its last look.
        if (m_saidBefore == SaidBefore::Look)
            m_said.clear();
        else if (m_saidBefore == SaidBefore::Tool && !m_said.isEmpty())
            m_said += QStringLiteral("\n\n");
        m_saidBefore = SaidBefore::Nothing;
        m_said += text;
        m_answer = AnswerText::parse(m_said);
        Q_EMIT answerChanged();
    });
    connect(client, &acp::Client::changeRefused, this, [this](const acp::ToolCall &call, const QString &) {
        // A proposal through the split-rock-change stand-in that the
        // assistant asked leave to run: the proposal itself goes on the card.
        if (std::optional<ChangeProposal> proposed = proposalIn(call)) {
            m_refused.insert(call.id);
            propose(*proposed);
            return;
        }
        const bool first = !m_refused.contains(call.id);
        m_refused.insert(call.id);
        // Told with the next question, so the assistant proposes instead.
        if (first && m_aside.isEmpty())
            m_aside = QStringLiteral("You asked leave to run \"%1\" yourself, and it was refused: nothing you run "
                                     "yourself changes this computer. Propose changes with split-rock-change.")
                          .arg(call.title.isEmpty() ? call.command() : call.title);
        m_card = ChangeCard::describe(call, m_current.name, m_said, m_question).toMap();
        m_cardOpen = true;
        Q_EMIT cardChanged();
        if (first) {
            m_refusedChanges.append(QVariantMap{{QStringLiteral("heading"), m_card.value(QStringLiteral("heading"))},
                                                {QStringLiteral("question"), m_question}});
            Q_EMIT turnsChanged();
        }
    });
    connect(client, &acp::Client::toolCallChanged, this, [this](const acp::ToolCall &call) {
        const bool writes = call.kind == QLatin1String("edit") || call.kind == QLatin1String("delete")
                         || call.kind == QLatin1String("move");
        if (call.status == QLatin1String("completed") && !writes && !m_refused.contains(call.id))
            m_saidBefore = SaidBefore::Look;
        else if (m_saidBefore == SaidBefore::Nothing)
            m_saidBefore = SaidBefore::Tool;
        // A change that finished without being asked for is what the fence in
        // Milestone 1 exists to stop; until then it is reported, plainly.
        const bool changesFiles = call.kind == QLatin1String("edit") || call.kind == QLatin1String("delete")
                               || call.kind == QLatin1String("move");
        if (changesFiles && call.status == QLatin1String("completed")
            && !m_refused.contains(call.id) && !m_allowed.contains(call.id) && !m_unasked.contains(call.id)) {
            m_unasked.insert(call.id);
            setNotice(QStringLiteral("%1 changed something without asking: %2")
                          .arg(m_current.name, call.title.isEmpty() ? call.kind : call.title));
        }
    });

    // Allowed only: a write inside the notebook, other than Split Rock's own
    // pages, with nothing in it that is never written down. Everything else
    // is refused, as before.
    // Also allowed: opening a settings skill, which the assistant's own
    // program may ask leave for because the stand-in writes outside its
    // working folder. It still runs inside the fence, and only reads.
    client->setAllow([this](const acp::ToolCall &call) {
        return mayWrite(call) || (!m_skillRequests.isEmpty() && !skillIn(call).isEmpty());
    });
    connect(client, &acp::Client::requestAllowed, this, [this](const acp::ToolCall &call) {
        m_allowed.insert(call.id);
        if (!skillIn(call).isEmpty())
            return;
        QStringList written;
        for (const QString &path : call.paths + call.diffPaths)
            written.append(QDir(m_notebook->folder()).relativeFilePath(path));
        written.removeDuplicates();
        setNotice(QStringLiteral("%1 wrote in the notebook: %2").arg(m_current.name, written.join(QStringLiteral(", "))));
        Q_EMIT notebookChanged();
    });

    Assistant serving = m_current;
    if (m_current.local && m_local) {
        // What Genie on this computer runs: the engine, the model that fits,
        // and whether it goes to the graphics chip.
        serving.environment.insert(QStringLiteral("SPLIT_ROCK_DESK_ENGINE"), m_local->engine());
        serving.environment.insert(QStringLiteral("SPLIT_ROCK_DESK_MODEL"), m_local->path());
        serving.environment.insert(QStringLiteral("SPLIT_ROCK_DESK_LAYERS"), m_local->layers());
        serving.readable.append(m_local->path());
    }
    acp::LaunchSpec launch = serveLaunch(serving, QProcessEnvironment::systemEnvironment(), m_workDir->path());
    if (!fenceProgram().isEmpty()) {
        FenceRoom room;
        room.devices = m_current.devices;
        room.writable = {m_current.home, m_workDir->path()};
        if (m_notebook)
            room.writable.append(m_notebook->folder());
        room.readable = serving.readable;
        if (!m_current.home.isEmpty()) {
            m_openRequests = QDir(m_current.home).filePath(QStringLiteral(".split-rock/open-requests"));
            QFile::remove(m_openRequests);
            m_openRequestsRead = 0;
            room.openRequests = m_openRequests;
            m_changeRequests = QDir(m_current.home).filePath(QStringLiteral(".split-rock/change-requests"));
            QFile::remove(m_changeRequests);
            m_changeRequestsRead = 0;
            room.changeRequests = m_changeRequests;
            if (!m_skills.isEmpty()) {
                m_skillRequests = QDir(m_current.home).filePath(QStringLiteral(".split-rock/skill-requests"));
                QFile::remove(m_skillRequests);
                m_skillRequestsRead = 0;
                room.skillRequests = m_skillRequests;
            }
            if (!m_changeRequestsTimer) {
                m_changeRequestsTimer = new QTimer(this);
                m_changeRequestsTimer->setInterval(300);
                connect(m_changeRequestsTimer, &QTimer::timeout, this, &Conversation::readChangeRequests);
                connect(m_changeRequestsTimer, &QTimer::timeout, this, &Conversation::readSkillRequests);
            }
            m_changeRequestsTimer->start();
        }
        launch = fenced(launch, room);
    } else if (!m_current.mayRunUnfenced) {
        setProblem(QStringLiteral("%1 can't be kept to the notebook on this computer.").arg(m_current.name),
                   QStringLiteral("The sandbox it runs in, bubblewrap, is missing."));
        return;
    }
    client->start(launch);
    client->initialize(clientVersion(), productTitle(), [this](const std::optional<acp::RpcError> &error) {
        if (error) {
            setProblem(QStringLiteral("%1 didn't start.").arg(m_current.name),
                       error->message + QLatin1Char('\n') + recentDiagnostics());
            return;
        }
        startSession();
    });
}

void Conversation::stopAssistant()
{
    if (!m_client)
        return;
    if (m_changeRequestsTimer)
        m_changeRequestsTimer->stop();
    acp::Client *client = std::exchange(m_client, nullptr);
    client->disconnect(this);
    client->kill();
    client->deleteLater();
    m_workDir.reset();
    removeConversationLeftovers(m_current);
}

void Conversation::startSession()
{
    m_client->newSession(m_workDir->path(), [this](const std::optional<acp::RpcError> &error) {
        if (error && error->code == acp::AuthRequiredCode) {
            enterSignIn(*error);
        } else if (error) {
            setProblem(QStringLiteral("%1 couldn't start a conversation.").arg(m_current.name),
                       error->message + QLatin1Char('\n') + recentDiagnostics());
        } else {
            askFirst();
        }
    });
}

void Conversation::askFirst()
{
    const QString wanted = m_current.askFirstMode;
    bool offered = false;
    for (const acp::Client::SessionMode &mode : m_client->modes())
        offered = offered || mode.id == wanted;
    if (wanted.isEmpty() || !offered || m_client->currentMode() == wanted) {
        setPhase(QStringLiteral("ready"));
        return;
    }
    m_client->setMode(wanted, [this](const std::optional<acp::RpcError> &error) {
        if (error) {
            // Refusing every request still holds; the mode only makes the
            // assistant ask more often.
            m_diagnostics.append(QStringLiteral("could not switch to the ask-first mode: ") + error->message);
        }
        setPhase(QStringLiteral("ready"));
    });
}

void Conversation::enterSignIn(const acp::RpcError &error)
{
    m_choices = classifySignIn(m_current, m_client->authMethods());
    m_signingIn = {};
    Q_EMIT signInChanged();
    if (signInChoices().isEmpty()) {
        QStringList why;
        for (const SignInChoice &choice : m_choices)
            why.append(choice.method.name + QStringLiteral(": ") + choice.whyNot);
        setProblem(QStringLiteral("None of %1's ways to sign in work without a terminal or a key that "
                                  "%2 would hold, so %2 can't offer it.").arg(m_current.name, productName()),
                   why.join(QLatin1Char('\n')));
        return;
    }
    if (error.reason() == QLatin1String("claude_subscription_not_supported"))
        // The adapter gives this reason whether or not a subscription is
        // signed in, so the words must hold either way.
        setNotice(QStringLiteral("%1 needs an Anthropic Console sign-in here. "
                                 "A Claude subscription can't be used in %2.").arg(m_current.name, productName()));
    setPhase(QStringLiteral("signIn"));
}

void Conversation::signIn(const QString &methodId)
{
    if (m_phase != QLatin1String("signIn") || !m_client)
        return;
    const auto it = std::find_if(m_choices.cbegin(), m_choices.cend(), [&methodId](const SignInChoice &c) {
        return c.method.id == methodId && c.how != SignInChoice::How::NotOffered;
    });
    if (it == m_choices.cend())
        return;
    m_signingIn = *it;
    m_signInPage.clear();
    m_problemDetails.clear();
    setNotice({});
    Q_EMIT signInChanged();
    setPhase(QStringLiteral("signingIn"));

    if (it->how == SignInChoice::How::WithoutTerminal) {
        signInWithoutTerminal(*it);
        return;
    }
    // The assistant's sign-in opens its page from inside the fence: read what
    // it asks for while the sign-in lasts.
    if (!m_openRequestsTimer) {
        m_openRequestsTimer = new QTimer(this);
        m_openRequestsTimer->setInterval(300);
        connect(m_openRequestsTimer, &QTimer::timeout, this, &Conversation::readOpenRequests);
    }
    m_openRequestsTimer->start();
    m_client->authenticate(methodId, [this](const std::optional<acp::RpcError> &error) {
        m_openRequestsTimer->stop();
        readOpenRequests();
        if (error) {
            signInDidNotFinish(error->message);
            return;
        }
        startSession();
    });
}

void Conversation::readOpenRequests()
{
    if (m_openRequests.isEmpty())
        return;
    QFile file(m_openRequests);
    if (!file.open(QIODevice::ReadOnly) || file.size() < m_openRequestsRead)
        return;
    file.seek(m_openRequestsRead);
    const QByteArray added = file.readAll();
    const qsizetype lastLine = added.lastIndexOf('\n');
    if (lastLine < 0)
        return;
    m_openRequestsRead += lastLine + 1;
    for (const QByteArray &line : added.left(lastLine).split('\n')) {
        const QString page = QString::fromUtf8(line).trimmed();
        if (!page.startsWith(QLatin1String("https://")) && !page.startsWith(QLatin1String("http://")))
            continue;
        // Only a sign-in opens a page: an assistant reaches no browser at any
        // other time.
        if (m_phase != QLatin1String("signingIn")) {
            m_diagnostics.append(QStringLiteral("did not open a page asked for outside sign-in"));
            continue;
        }
        m_signInPage = page;
        Q_EMIT signInChanged();
        Q_EMIT signInPageReady(page);
    }
}

void Conversation::signInWithoutTerminal(const SignInChoice &choice)
{
    m_signInOutput.clear();
    m_signInProcess = new QProcess(this);
    const acp::LaunchSpec spec = signInLaunch(m_current, choice.method, QProcessEnvironment::systemEnvironment(),
                                              m_workDir->path());
    m_signInProcess->setProgram(spec.program);
    m_signInProcess->setArguments(spec.arguments);
    m_signInProcess->setProcessEnvironment(spec.environment);
    m_signInProcess->setWorkingDirectory(spec.workingDirectory);
    m_signInProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_signInProcess, &QProcess::started, m_signInProcess, &QProcess::closeWriteChannel);
    connect(m_signInProcess, &QProcess::readyRead, this, [this] {
        m_signInOutput += QString::fromUtf8(m_signInProcess->readAll());
        static const QRegularExpression url(QStringLiteral("https?://[^\\s\"'<>]+"));
        const QRegularExpressionMatch match = url.match(m_signInOutput);
        if (match.hasMatch() && m_signInPage.isEmpty()) {
            m_signInPage = match.captured(0);
            Q_EMIT signInChanged();
        }
    });
    const auto finish = [this](bool signedIn) {
        const QString output = m_signInOutput.right(2000);
        stopSignInProcess();
        if (signedIn) {
            // A fresh start reads the new sign-in.
            stopAssistant();
            startAssistant();
            return;
        }
        signInDidNotFinish(output);
    };
    connect(m_signInProcess, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit && code == 0);
    });
    connect(m_signInProcess, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false);
    });
    m_signInTimeout = new QTimer(this);
    m_signInTimeout->setSingleShot(true);
    connect(m_signInTimeout, &QTimer::timeout, this, [finish] { finish(false); });
    m_signInTimeout->start(SignInLimitMs);
    m_signInProcess->start();
}

void Conversation::signInDidNotFinish(const QString &details)
{
    m_notice = QStringLiteral("Signing in didn't finish. You can try again.");
    m_problemDetails = details.trimmed();
    Q_EMIT noticeChanged();
    setPhase(QStringLiteral("signIn"));
}

void Conversation::stopSignInProcess()
{
    if (m_signInTimeout) {
        m_signInTimeout->stop();
        m_signInTimeout->deleteLater();
        m_signInTimeout = nullptr;
    }
    if (!m_signInProcess)
        return;
    QProcess *process = std::exchange(m_signInProcess, nullptr);
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        process->kill();
        process->waitForFinished(1500);
    }
    process->deleteLater();
}

void Conversation::cancelSignIn()
{
    if (m_phase != QLatin1String("signingIn"))
        return;
    if (m_signInProcess) {
        stopSignInProcess();
        m_signingIn = {};
        Q_EMIT signInChanged();
        setPhase(QStringLiteral("signIn"));
        return;
    }
    // A sign-in the agent is running has no way to be called off but stopping
    // the agent.
    stopAssistant();
    startAssistant();
}

void Conversation::ask(const QString &text)
{
    const QString question = text.trimmed();
    if (question.isEmpty())
        return;
    // Asked before the assistant is ready, as from Search the moment it opens:
    // the question waits, shown, and goes as soon as the assistant can take it.
    if (!m_current.id.isEmpty() && !m_choosing
        && (m_phase == QLatin1String("starting") || m_phase == QLatin1String("download")
            || m_phase == QLatin1String("signIn")
            || m_phase == QLatin1String("signingIn"))) {
        keepTurn();
        clearAnswer();
        m_question = question;
        m_waiting = question;
        Q_EMIT answerChanged();
        return;
    }
    if (m_phase != QLatin1String("ready"))
        return;
    // A card left open would sit under the answer and push it out of view; a
    // new question puts it away, unless it is still working.
    for (const auto &[name, chore] : m_chores) {
        if (!chore->busy())
            chore->close();
    }
    if (m_current.guide) {
        answerFromGuide(question);
        return;
    }
    if (!m_client)
        return;
    // Split Rock's hint at the skill the question may need; the assistant
    // makes the pick, and may open another or none.
    if (!m_skillRequests.isEmpty()) {
        if (const Skill *skill = m_skills.find(question)) {
            if (!m_aside.isEmpty())
                m_aside += QLatin1Char('\n');
            m_aside += QStringLiteral("A settings skill that may fit this question: %1. Open it if it helps; "
                                      "ignore this if it doesn't.").arg(skill->id);
        }
    }
    // Genie on this computer answers from the guide, as written, where an
    // entry fits: it was written and checked ahead of time, and a small model
    // retelling it can get it wrong.
    if (m_current.local && !m_guide.find(question, m_guideContext, 1).isEmpty()) {
        answerFromGuide(question);
        return;
    }
    send(question, question);
}

QList<GuideEntry> Conversation::tours() const
{
    // A desktop's own tours first, as they show what the person will see
    // first, then the general ones.
    QList<GuideEntry> list = m_guide.entries(m_guideContext, GuideEntry::Kind::Tour);
    std::stable_sort(list.begin(), list.end(), [](const GuideEntry &a, const GuideEntry &b) {
        const bool aGeneral = a.source == QLatin1String("plasma");
        const bool bGeneral = b.source == QLatin1String("plasma");
        if (aGeneral != bGeneral)
            return !aGeneral;
        return a.key() < b.key();
    });
    return list;
}

QString Conversation::nextTour() const
{
    const QList<GuideEntry> list = tours();
    return m_tour >= 0 && m_tour + 1 < list.size() ? list.at(m_tour + 1).title : QString();
}

void Conversation::showTour()
{
    if (!m_current.guide || m_phase != QLatin1String("ready"))
        return;
    const QList<GuideEntry> list = tours();
    if (list.isEmpty())
        return;
    const int next = m_tour + 1 < list.size() ? m_tour + 1 : 0;
    const QString question = m_question.isEmpty() ? QStringLiteral("Show me around") : m_question;
    keepTurn();
    clearAnswer();
    setNotice({});
    m_question = question;
    m_tour = next;
    const GuideEntry &tour = list.at(next);
    m_answer.headline = tour.title;
    m_lead = tour.intro;
    m_answer.steps = tour.steps;
    Q_EMIT answerChanged();
}

void Conversation::answerFromGuide(const QString &question)
{
    static const QRegularExpression tourWords(QStringLiteral("\\b(show me around|tour)\\b"),
                                              QRegularExpression::CaseInsensitiveOption);
    keepTurn();
    if (tourWords.match(question).hasMatch() && !tours().isEmpty()) {
        clearAnswer();
        m_question = question;
        showTour();
        return;
    }
    clearAnswer();
    setNotice({});
    m_question = question;
    const QList<GuideEntry> found = m_guide.find(question, m_guideContext);
    if (found.isEmpty()) {
        m_answer.headline = QStringLiteral("%1's guide has nothing on that yet.").arg(productName());
        m_answer.more = {QStringLiteral("Try it in other words, or one of the problems above. With an assistant "
                                        "account, %1 can answer anything about this computer.").arg(productName())};
    } else {
        const GuideEntry &entry = found.first();
        m_answer.headline = entry.title;
        m_answer.steps = entry.steps;
        if (!entry.why.isEmpty())
            m_answer.more = entry.why.split(QStringLiteral("\n\n"));
    }
    Q_EMIT answerChanged();
}

void Conversation::send(const QString &prompt, const QString &shown, const QJsonObject &meta)
{
    if (!m_client)
        return;
    keepTurn();
    clearAnswer();
    m_question = shown;
    Q_EMIT answerChanged();
    setNotice({});
    setPhase(QStringLiteral("working"));

    // Each block says what it is under _meta, which assistants that don't
    // know it pass over, and Genie on this computer reads.
    const auto block = [](const QString &text, const QJsonObject &label) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                           {QStringLiteral("text"), text},
                           {QStringLiteral("_meta"), QJsonObject{{QStringLiteral("splitRock"), label}}}};
    };
    const auto kind = [](const char *name) { return QJsonObject{{QStringLiteral("kind"), QLatin1String(name)}}; };
    QJsonArray blocks;
    if (!m_framed) {
        blocks.append(block(framing() + QStringLiteral("\n\n") + settingsWords(), framingMeta()));
        // What the notebook remembers, as the conversation starts.
        const QString memory = m_notebook ? m_notebook->memory() : QString();
        if (!memory.isEmpty())
            blocks.append(block(memory, kind("memory")));
        m_framed = true;
    }
    if (!m_aside.isEmpty())
        blocks.append(block(std::exchange(m_aside, QString()), kind("aside")));
    blocks.append(block(prompt, meta.isEmpty() ? kind("question") : meta));
    m_handOff.clear();

    m_client->prompt(blocks, [this](const QString &stopReason, const std::optional<acp::RpcError> &error) {
        if (error && error->code == acp::AuthRequiredCode) {
            enterSignIn(*error);
            return;
        }
        if (error && error->code == acp::AgentStoppedCode)
            return; // Reported by the stop itself.
        setPhase(QStringLiteral("ready"));
        // Genie on this computer asks for the bigger assistant.
        if (!error && m_current.local && m_client
            && m_client->lastPromptMeta().value(QStringLiteral("splitRock")).toObject().value(QStringLiteral("handOff")).toBool()) {
            if (const Assistant *target = handOffTarget()) {
                m_handOff = target->name;
                Q_EMIT answerChanged();
            }
        }
        if (error) {
            setNotice(QStringLiteral("%1 couldn't answer. %2").arg(m_current.name, error->message));
        } else if (stopReason == QLatin1String("refusal")) {
            setNotice(QStringLiteral("%1 would not answer that.").arg(m_current.name));
        } else if (stopReason == QLatin1String("max_tokens") || stopReason == QLatin1String("max_turn_requests")) {
            setNotice(QStringLiteral("The answer was cut short."));
        } else if (stopReason == QLatin1String("cancelled")) {
            setNotice(QStringLiteral("Stopped."));
        } else if (m_said.trimmed().isEmpty() && m_card.isEmpty()) {
            setNotice(QStringLiteral("%1 gave no answer.").arg(m_current.name));
        }
    });
}

void Conversation::stop()
{
    if (!m_waiting.isEmpty()) {
        m_waiting.clear();
        m_question.clear();
        Q_EMIT answerChanged();
        return;
    }
    if (m_phase == QLatin1String("working") && m_client)
        m_client->cancel();
}

void Conversation::readChangeRequests()
{
    if (m_changeRequests.isEmpty())
        return;
    QFile file(m_changeRequests);
    if (!file.open(QIODevice::ReadOnly) || file.size() < m_changeRequestsRead)
        return;
    file.seek(m_changeRequestsRead);
    const QByteArray added = file.readAll();
    const qsizetype lastLine = added.lastIndexOf('\n');
    if (lastLine < 0)
        return;
    m_changeRequestsRead += lastLine + 1;
    for (const QByteArray &line : added.left(lastLine).split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        QString error;
        if (std::optional<ChangeProposal> change = ChangeProposal::fromJson(QByteArray::fromBase64(line.trimmed()), &error))
            propose(*change);
        else
            m_diagnostics.append(QStringLiteral("a proposed change could not be read: %1").arg(error));
    }
}

QString Conversation::settingsWords() const
{
    // Without the stand-in, how the desktop looks, which the fence keeps the
    // assistant from reading, is told up front.
    if (m_skillRequests.isEmpty())
        return Appearance::current().describe();
    return QStringLiteral("Settings skills: before you propose a change to a setting, open the skill for it by "
                          "running split-rock-skill with its name. It tells you how to change, undo and check that "
                          "setting, and shows you its value now and the names installed on this computer, which your "
                          "sandbox cannot see. Use those names exactly. Open only the skill you need; when none "
                          "fits, look around yourself as before. The shelf:\n%1").arg(m_skills.shelf());
}

void Conversation::readSkillRequests()
{
    if (m_skillRequests.isEmpty())
        return;
    QFile file(m_skillRequests);
    if (!file.open(QIODevice::ReadOnly) || file.size() < m_skillRequestsRead)
        return;
    file.seek(m_skillRequestsRead);
    const QByteArray added = file.readAll();
    const qsizetype lastLine = added.lastIndexOf('\n');
    if (lastLine < 0)
        return;
    m_skillRequestsRead += lastLine + 1;
    static const QRegularExpression request(QStringLiteral("^([0-9.]{1,40}) ([a-z0-9-]{1,64})$"));
    for (const QByteArray &line : added.left(lastLine).split('\n')) {
        const QRegularExpressionMatch match = request.match(QString::fromUtf8(line.trimmed()));
        if (match.hasMatch())
            openSkill(match.captured(1), match.captured(2));
        else if (!line.trimmed().isEmpty())
            m_diagnostics.append(QStringLiteral("a skill request could not be read"));
    }
}

void Conversation::openSkill(const QString &token, const QString &id)
{
    const QString answer = QFileInfo(m_skillRequests).dir().filePath(QStringLiteral("skill-%1.out").arg(token));
    const auto write = [answer](const QString &text) {
        // Written whole, then put in place, so the stand-in never reads half.
        const QString part = answer + QStringLiteral(".part");
        QFile file(part);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(text.toUtf8()) >= 0) {
            file.close();
            QFile::remove(answer);
            QFile::rename(part, answer);
        }
    };
    qInfo().noquote() << QStringLiteral("%1: the assistant opened the %2 skill").arg(productName(), id);
    m_skills.open(id, this, write);
}

void Conversation::propose(const ChangeProposal &proposed)
{
    // A whole-system update is never made from the Do it card: it can outrun
    // the time a change is given and be stopped halfway, it can stop to ask
    // questions no one is there to answer, and a snapshot pair can't put a new
    // kernel back while the computer runs on it. Split Rock opens its own
    // Update card instead, and the assistant is told so.
    const QString updater = proposed.wholeSystemUpdate();
    if (!updater.isEmpty()) {
        qInfo().noquote() << QStringLiteral("%1: opened the Update card for a proposal that updates the whole system with %2")
                                 .arg(productName(), updater);
        openChore(QStringLiteral("update"));
        m_report = QStringLiteral("Your proposed change \"%1\" updates the whole system with %2, or fetches the package "
                                  "lists without updating. %3 never runs either from a card; it opened its own Update "
                                  "card instead, which looks at what is waiting, takes a restore point, shows the "
                                  "progress and tidies up after. The person decides there. Never fetch the package "
                                  "lists without updating, which leaves the system half updated. Don't propose the "
                                  "update again; tell the person in one or two plain sentences that the Update card "
                                  "is open, and propose single changes only.")
                       .arg(proposed.title, updater, productName());
        m_reportShown = QStringLiteral("Opened the Update card");
        m_reportMeta = {{QStringLiteral("kind"), QStringLiteral("update")}, {QStringLiteral("ok"), true}};
        if (m_client && m_phase == QLatin1String("ready"))
            sendReport();
        return;
    }
    // One of Split Rock's own cards, asked for by name.
    if (!proposed.card.isEmpty()) {
        const bool opened = openChore(proposed.card, proposed.query);
        qInfo().noquote() << QStringLiteral("%1: the assistant asked for the %2 card").arg(productName(), proposed.card);
        m_report = opened
            ? QStringLiteral("%1 opened its own %2 card, as you asked. The person decides there; %1 tells you how it "
                             "went. Tell the person in one plain sentence that the card is open.")
                  .arg(productName(), proposed.card)
            : QStringLiteral("%1 couldn't open its %2 card: another card is busy. Tell the person to wait for it to "
                             "finish.").arg(productName(), proposed.card);
        m_reportShown = opened ? QStringLiteral("Opened a card") : QStringLiteral("Another card is busy");
        m_reportMeta = {{QStringLiteral("kind"), QStringLiteral("card")}, {QStringLiteral("ok"), opened}};
        if (m_client && m_phase == QLatin1String("ready"))
            sendReport();
        return;
    }
    // No card runs a command as the administrator: the proposal is not shown,
    // and the assistant is told what can be made instead (docs/DECISIONS.md,
    // No card runs a command as the administrator).
    const QString elevation = proposed.elevation();
    if (!elevation.isEmpty()) {
        qInfo().noquote() << QStringLiteral("%1: refused a proposal that reaches for the administrator (%2)")
                                 .arg(productName(), elevation);
        m_report = QStringLiteral("Your proposed change \"%1\" needs the administrator (%2), so it was not shown to the "
                                  "person. %3 never runs an assistant's command as the administrator. Changes to the "
                                  "system are made only by %3's own cards: update, space, install, restore and "
                                  "community. Propose one of those with \"card\" when it fits; otherwise tell the person "
                                  "in one or two plain sentences that this is a change %3 can't make for them.")
                       .arg(proposed.title,
                            elevation == QLatin1String("admin") ? QStringLiteral("\"admin\": true") : elevation,
                            productName());
        m_reportShown = QStringLiteral("Not shown: it needed the administrator");
        m_reportMeta = {{QStringLiteral("kind"), QStringLiteral("refused")}, {QStringLiteral("ok"), false}};
        if (m_client && m_phase == QLatin1String("ready"))
            sendReport();
        return;
    }
    const Appearance now = Appearance::current();
    // A theme named loosely is the one installed theme it fits, so the card
    // shows the name that will be applied.
    ChangeProposal settled = proposed;
    settled.command = now.resolve(proposed.command);
    // A theme that fits no installed one, or several, can't be applied; the
    // assistant is asked again with the names that are, and no card is shown.
    const QString unknown = now.unknownName(proposed.command);
    if (!unknown.isEmpty()) {
        qInfo().noquote() << QStringLiteral("%1: refused a proposal naming \"%2\", which is not installed")
                                 .arg(productName(), unknown);
        m_report = QStringLiteral("Your proposed change \"%1\" names \"%2\", which is not installed on this computer, "
                                  "so it was not shown to the person. Propose it again with an installed name, or tell "
                                  "the person it isn't installed.\n%3")
                       .arg(proposed.title, unknown, now.describe());
        m_reportShown = QStringLiteral("Checking the name: %1 isn't installed").arg(unknown);
        m_reportMeta = {{QStringLiteral("kind"), QStringLiteral("refused")}, {QStringLiteral("ok"), false}};
        if (m_client && m_phase == QLatin1String("ready"))
            sendReport();
        return;
    }
    const ChangeProposal change = ChangeRunner::withKnownUndo(settled, now);
    m_proposal = change;
    for (const auto &[name, chore] : m_chores)
        chore->close();
    m_card = ChangeCard::forProposal(change, m_current.name).toMap();
    m_cardOpen = true;
    Q_EMIT cardChanged();
}

void Conversation::doIt()
{
    if (!m_proposal || m_runner->busy() || choreBusy())
        return;
    // Written down, with how to undo it, before anything runs.
    QString unrecorded;
    const auto writeDown = [this, &unrecorded](const ChangeRunner::Record &record) {
        if (!m_notebook)
            return true;
        const QString undo = record.undoWay();
        const bool written = m_notebook->recordChange(record.id, record.proposal.title, record.proposal.why, undo,
                                                      QStringLiteral("Making"), &unrecorded);
        Q_EMIT notebookChanged();
        return written;
    };
    if (m_runner->make(*m_proposal, writeDown).isEmpty()) {
        Q_EMIT changesChanged();
        setNotice(unrecorded.isEmpty()
                      ? QStringLiteral("Nothing was changed: %1 can't make this change safely on this computer.").arg(productName())
                      : QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded));
        return;
    }
    m_card.insert(QStringLiteral("making"), true);
    Q_EMIT cardChanged();
    Q_EMIT changesChanged();
    setNotice(QStringLiteral("Making the change."));
}

bool Conversation::undoChange(const QString &id)
{
    QString error;
    if (!m_runner->undo(id, &error)) {
        setNotice(error);
        return false;
    }
    Q_EMIT changesChanged();
    setNotice(QStringLiteral("Putting it back."));
    return true;
}

QVariantList Conversation::changes() const
{
    QVariantList list;
    for (const ChangeRunner::Record &r : m_runner->records()) {
        list.append(QVariantMap{{QStringLiteral("id"), r.id},
                                {QStringLiteral("title"), r.proposal.title},
                                {QStringLiteral("date"), r.made.date().toString(Qt::ISODate)},
                                {QStringLiteral("status"), r.status},
                                {QStringLiteral("undoWay"), r.undoWay()},
                                {QStringLiteral("canUndo"), r.canUndo()}});
    }
    return list;
}

bool Conversation::changing() const
{
    return m_runner->busy();
}

void Conversation::showMeHow()
{
    // The guide answers from what is written; only an assistant can be asked.
    if (m_phase != QLatin1String("ready") || m_current.guide || !m_client)
        return;
    m_cardOpen = false;
    Q_EMIT cardChanged();
    send(QStringLiteral("Don't change anything. Tell me how to do that myself, in short steps."),
         QStringLiteral("Show me how instead"));
}

void Conversation::notNow()
{
    m_cardOpen = false;
    Q_EMIT cardChanged();
}

void Conversation::openCard()
{
    if (m_card.isEmpty())
        return;
    m_cardOpen = true;
    Q_EMIT cardChanged();
}

void Conversation::newQuestion()
{
    if (m_current.id.isEmpty() || m_phase == QLatin1String("signingIn"))
        return;
    if (m_current.guide) {
        clearAnswer();
        return;
    }
    // The conversation ends here and is not saved: the assistant starts over
    // and its own record of the conversation is removed.
    stopAssistant();
    clearAnswer();
    clearTurns();
    startAssistant();
}

void Conversation::setPhase(const QString &phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    if (phase == QLatin1String("problem"))
        m_waiting.clear();
    Q_EMIT phaseChanged();
    if (phase == QLatin1String("ready") && !m_report.isEmpty() && m_client) {
        QTimer::singleShot(0, this, [this] {
            if (m_phase != QLatin1String("ready") || m_report.isEmpty() || !m_client)
                return;
            sendReport();
        });
        return;
    }
    if (phase == QLatin1String("ready") && !m_waiting.isEmpty()) {
        QTimer::singleShot(0, this, [this] {
            if (m_phase != QLatin1String("ready") || m_waiting.isEmpty())
                return;
            const QString question = std::exchange(m_waiting, QString());
            ask(question);
        });
    }
}

void Conversation::setNotice(const QString &notice)
{
    if (m_notice == notice)
        return;
    m_notice = notice;
    Q_EMIT noticeChanged();
}

void Conversation::setProblem(const QString &problem, const QString &details)
{
    m_problem = problem;
    m_problemDetails = details.trimmed();
    Q_EMIT noticeChanged();
    if (!problem.isEmpty())
        setPhase(QStringLiteral("problem"));
}

void Conversation::clearAnswer()
{
    m_tour = -1;
    m_lead.clear();
    m_question.clear();
    m_waiting.clear();
    m_said.clear();
    m_saidBefore = SaidBefore::Nothing;
    m_answer = {};
    m_card.clear();
    m_cardOpen = false;
    m_proposal.reset();
    m_handOff.clear();
    m_kept = false;
    m_fixed = false;
    m_rememberAnswered = false;
    m_refused.clear();
    m_allowed.clear();
    m_unasked.clear();
    Q_EMIT answerChanged();
    Q_EMIT cardChanged();
}

void Conversation::keepTurn()
{
    if (m_question.isEmpty() || (m_answer.headline.isEmpty() && m_answer.steps.isEmpty() && m_answer.more.isEmpty()))
        return;
    m_turns.append(QVariantMap{{QStringLiteral("question"), m_question},
                               {QStringLiteral("headline"), m_answer.headline},
                               {QStringLiteral("steps"), m_answer.steps},
                               {QStringLiteral("more"), m_answer.more}});
    Q_EMIT turnsChanged();
}

void Conversation::clearTurns()
{
    m_turns.clear();
    m_refusedChanges.clear();
    Q_EMIT turnsChanged();
}

bool Conversation::mayWrite(const acp::ToolCall &call) const
{
    if (!m_notebook || call.kind != QLatin1String("edit"))
        return false;
    const QStringList paths = call.paths + call.diffPaths;
    if (paths.isEmpty())
        return false;
    for (const QString &path : paths) {
        if (!QDir::isAbsolutePath(path) || !m_notebook->contains(path))
            return false;
        // Split Rock's own pages are written by Split Rock alone.
        const QString relative = QDir(m_notebook->folder()).relativeFilePath(path);
        // About me.md is written by Split Rock too, after the person's yes.
        if (relative == QLatin1String("This computer.md") || relative == QLatin1String("ASSISTANT.md")
            || relative == QLatin1String("About me.md"))
            return false;
    }
    const QString content = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("input"), call.rawInput}})
                                                  .toJson(QJsonDocument::Compact));
    return neverWrittenReason(content).isEmpty();
}

void Conversation::setNotebook(Notebook *notebook)
{
    m_notebook = notebook;
    Q_EMIT notebookChanged();
}

QString Conversation::answerText() const
{
    if (!m_said.trimmed().isEmpty())
        return m_said.trimmed();
    // An answer from the guide, written out as it is shown.
    QStringList parts;
    if (!m_answer.headline.isEmpty())
        parts.append(m_answer.headline);
    if (!m_lead.isEmpty())
        parts.append(m_lead);
    QStringList steps;
    for (int i = 0; i < m_answer.steps.size(); ++i)
        steps.append(QStringLiteral("%1. %2").arg(i + 1).arg(m_answer.steps.at(i)));
    if (!steps.isEmpty())
        parts.append(steps.join(QLatin1Char('\n')));
    parts.append(m_answer.more);
    return parts.join(QStringLiteral("\n\n"));
}

bool Conversation::keep()
{
    if (!m_notebook || m_question.isEmpty() || m_phase != QLatin1String("ready") || m_kept)
        return m_kept;
    const QString answer = answerText();
    if (answer.isEmpty())
        return false;
    QString error;
    if (m_notebook->keep(m_question, answer, &error).isEmpty()) {
        setNotice(error);
        return false;
    }
    m_kept = true;
    Q_EMIT answerChanged();
    Q_EMIT notebookChanged();
    return true;
}

bool Conversation::forget(const QString &path)
{
    QString error;
    if (!m_notebook || !m_notebook->forget(path, &error)) {
        setNotice(error);
        return false;
    }
    Q_EMIT notebookChanged();
    return true;
}

QString Conversation::aboutMe() const
{
    if (!m_notebook)
        return {};
    QFile file(QDir(m_notebook->folder()).filePath(QStringLiteral("About me.md")));
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

QVariantList Conversation::fixedProblems() const
{
    QVariantList list;
    if (!m_notebook)
        return list;
    for (const Notebook::Fixed &fixed : m_notebook->fixedProblems())
        list.append(QVariantMap{{QStringLiteral("when"), fixed.when}, {QStringLiteral("text"), fixed.text}});
    return list;
}

QVariantList Conversation::deviceFixes() const
{
    QVariantList list;
    if (!m_notebook)
        return list;
    for (const Notebook::DeviceFix &fix : m_notebook->deviceFixes())
        list.append(QVariantMap{{QStringLiteral("path"), fix.path}, {QStringLiteral("title"), fix.title},
                                {QStringLiteral("why"), fix.why},
                                {QStringLiteral("steps"), fix.steps}});
    return list;
}

QStringList Conversation::deviceStillOpen() const
{
    return m_notebook ? m_notebook->deviceStillOpen() : QStringList();
}

bool Conversation::solveStillOpen(const QString &problem, const QString &fix)
{
    if (!m_notebook)
        return false;
    QString error;
    if (!m_notebook->solveStillOpen(problem, fix, &error)) {
        setNotice(error);
        return false;
    }
    Q_EMIT notebookChanged();
    return true;
}

bool Conversation::reopenFix(const QString &path)
{
    if (!m_notebook)
        return false;
    QString error;
    const bool reopened = m_notebook->reopenFix(path, &error);
    if (!error.isEmpty())
        setNotice(error);
    Q_EMIT notebookChanged();
    return reopened;
}

bool Conversation::rememberYes()
{
    const QString sentence = remember();
    if (!m_notebook || sentence.isEmpty())
        return false;
    QString error;
    if (!m_notebook->rememberAboutPerson(sentence, &error)) {
        setNotice(error);
        return false;
    }
    m_rememberAnswered = true;
    setNotice(QStringLiteral("Remembered in About me."));
    Q_EMIT answerChanged();
    Q_EMIT notebookChanged();
    return true;
}

void Conversation::rememberNo()
{
    if (remember().isEmpty())
        return;
    m_rememberAnswered = true;
    Q_EMIT answerChanged();
}

bool Conversation::markFixed()
{
    if (!m_notebook || m_question.isEmpty() || m_phase != QLatin1String("ready") || m_fixed)
        return m_fixed;
    // The problem in the person's own words is what they first asked; what
    // fixed it is this answer's lead.
    QString problem = m_turns.isEmpty() ? m_question : m_turns.first().toMap().value(QStringLiteral("question")).toString();
    if (problem == QLatin1String("Show me how instead"))
        problem = m_question;
    QString fix = m_answer.headline.isEmpty() ? m_answer.more.value(0) : m_answer.headline;
    if (fix.isEmpty() && !m_answer.steps.isEmpty())
        fix = m_answer.steps.join(QStringLiteral(" "));
    if (fix.isEmpty())
        return false;
    QString error;
    if (!m_notebook->recordFixed(problem, fix, &error)) {
        setNotice(error);
        return false;
    }
    m_fixed = true;
    Q_EMIT answerChanged();
    Q_EMIT notebookChanged();
    return true;
}

QVariantList Conversation::keptNotes() const
{
    QVariantList list;
    if (!m_notebook)
        return list;
    for (const Notebook::Kept &note : m_notebook->kept()) {
        list.append(QVariantMap{{QStringLiteral("title"), note.title},
                                {QStringLiteral("topic"), note.topic},
                                {QStringLiteral("path"), note.path},
                                {QStringLiteral("date"), note.date}});
    }
    return list;
}

QString Conversation::recentDiagnostics() const
{
    return m_diagnostics.join(QLatin1Char('\n'));
}

} // namespace splitrock
