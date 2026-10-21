// SPDX-License-Identifier: GPL-2.0-or-later
#include "codex/Chat.h"

#include "app/Appearance.h"
#include "app/ChangeRunner.h"
#include "app/Notebook.h"
#include "app/Product.h"
#include "ask/Fence.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace splitrock::codex {

namespace {

// The desktop's own settings, which Codex reads to explain the computer.
// Personal files, and settings that can hold another program's tokens, stay
// hidden.
const QStringList DesktopSettings{
    QStringLiteral("kdeglobals"), QStringLiteral("kwinrc"), QStringLiteral("kwinoutputconfig.json"),
    QStringLiteral("plasmashellrc"), QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"),
    QStringLiteral("plasma-localerc"), QStringLiteral("kcminputrc"), QStringLiteral("kxkbrc"),
    QStringLiteral("touchpadxlibinputrc"), QStringLiteral("powerdevilrc"), QStringLiteral("powermanagementprofilesrc"),
    QStringLiteral("bluedevilglobalrc"), QStringLiteral("kscreenlockerrc"), QStringLiteral("ksmserverrc"),
    QStringLiteral("kded6rc"), QStringLiteral("baloofilerc"), QStringLiteral("dolphinrc"), QStringLiteral("katerc"),
    QStringLiteral("mimeapps.list"), QStringLiteral("user-dirs.dirs"), QStringLiteral("user-dirs.locale"),
    QStringLiteral("plasmanotifyrc"), QStringLiteral("pipewire"), QStringLiteral("wireplumber"),
    QStringLiteral("fontconfig"), QStringLiteral("gtk-3.0/settings.ini"), QStringLiteral("gtk-4.0/settings.ini"),
    QStringLiteral("autostart"), QStringLiteral("environment.d"), QStringLiteral("systemd/user"),
};

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

bool writeScript(const QString &path, const QByteArray &script)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(script) != script.size())
        return false;
    file.close();
    return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

// The words that make up what was said, newest last, for a note.
QString transcript(const QVariantList &turns)
{
    QStringList lines;
    for (const QVariant &turn : turns) {
        const QVariantMap t = turn.toMap();
        const QString text = t.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty())
            continue;
        lines.append((t.value(QStringLiteral("who")).toString() == QLatin1String("you") ? QStringLiteral("You: ")
                                                                                         : QStringLiteral("Codex: "))
                     + text);
    }
    return lines.join(QStringLiteral("\n\n"));
}

} // namespace

Chat::Chat(const Agent &agent, ChangeRunner *runner, Notebook *notebook, Hands hands, QObject *parent)
    : QObject(parent)
    , m_agent(agent)
    , m_runner(runner)
    , m_notebook(notebook)
    , m_hands(std::move(hands))
    , m_fence(ask::fenceProgram())
{
    m_poll.setInterval(300);
    connect(&m_poll, &QTimer::timeout, this, &Chat::readRequests);
    m_signInTimer.setSingleShot(true);
    connect(&m_signInTimer, &QTimer::timeout, this, [this] {
        if (m_phase != QLatin1String("signingIn"))
            return;
        // The agent has no way to call a sign-in off but stopping it.
        end();
        setNotice(QStringLiteral("Signing in didn't finish. You can try again."));
        start();
    });
    if (m_runner) {
        connect(m_runner, &ChangeRunner::made, this, [this](const ChangeRunner::Record &record) {
            if (record.id != m_changeId)
                return;
            const bool ok = record.status == QLatin1String("done");
            if (m_notebook)
                m_notebook->markChange(record.id, ok ? QStringLiteral("Done") : QStringLiteral("Failed"));
            m_proposal.insert(QStringLiteral("state"), ok ? QStringLiteral("done") : QStringLiteral("failed"));
            m_proposal.insert(QStringLiteral("canUndo"), record.canUndo());
            Q_EMIT proposalChanged();
            setNotice(ok ? QStringLiteral("Done: %1.").arg(record.proposal.title)
                         : QStringLiteral("That didn't work: %1. Nothing else was changed.").arg(record.proposal.title));
            QString output = record.output;
            if (output.size() > 3000)
                output = QStringLiteral("...") + output.right(3000);
            const QString checked = record.checked.isEmpty()
                ? QStringLiteral("You gave no check, so nothing was read back from the running desktop. A command "
                                 "that succeeds does not prove the change took effect, so don't say it worked: ask "
                                 "the person.")
                : QStringLiteral("Your check, run after the change, read back from the running desktop:\n%1\n"
                                 "Say it worked only if this shows the change took effect.").arg(record.checked.right(2000));
            report(QStringLiteral("The person tapped Do it, and %1 ran your proposed change \"%2\" as the person. "
                                  "Exit code %3. Output:\n%4\n\n%5\n\nTell the person in plain words how it went. You "
                                  "cannot see their screen or running programs, so when the change is something only "
                                  "they can see, ask them whether it worked. Don't propose the same change again "
                                  "unless it failed and you know why.")
                       .arg(productTitle(), record.proposal.title)
                       .arg(record.exitCode)
                       .arg(output.isEmpty() ? QStringLiteral("(none)") : output, checked));
        });
        connect(m_runner, &ChangeRunner::undone, this, [this](const ChangeRunner::Record &record) {
            if (record.id != m_changeId)
                return;
            const bool ok = record.status == QLatin1String("undone");
            if (m_notebook)
                m_notebook->markChange(record.id, ok ? QStringLiteral("Undone %1").arg(QDate::currentDate().toString(Qt::ISODate))
                                                     : QStringLiteral("Undo failed"));
            m_proposal.insert(QStringLiteral("state"), ok ? QStringLiteral("undone") : QStringLiteral("undoFailed"));
            m_proposal.insert(QStringLiteral("canUndo"), false);
            Q_EMIT proposalChanged();
            setNotice(ok ? QStringLiteral("Undone: %1.").arg(record.proposal.title)
                         : QStringLiteral("The undo didn't work: %1.").arg(record.proposal.title));
            m_aside = QStringLiteral("Since your last answer the person tapped Undo on \"%1\", and %2.")
                          .arg(record.proposal.title,
                               ok ? QStringLiteral("it was put back") : QStringLiteral("putting it back did not work"));
        });
    }
}

Chat::~Chat()
{
    end();
}

QString Chat::framing()
{
    return QStringLiteral(
        "You are Codex, answering in a window of %1's on a Linux computer with the KDE Plasma desktop. The person "
        "asked their computer first, and it handed this question to you as a bigger one. They have used Windows for "
        "years and are new to Linux. Answer in plain words, with no jargon, and name the Windows equivalent when it "
        "helps. Start with one short sentence, then give at most five short numbered steps when steps help. "
        "You run in a sandbox and cannot change this computer yourself; you may look. To change a setting, a file or "
        "the system, propose it: run split-rock-change with one JSON argument, {\"title\": a short instruction such "
        "as \"Turn off Wi-Fi power saving\", \"what\": what will change, in plain words, \"why\": why, in plain words, "
        "\"command\": the shell command that makes the change, which runs as the person and never as the "
        "administrator (never put sudo, pkexec or any other way to the administrator in it), \"undo\": a command "
        "that puts it back, when there is one, applied live the same way, \"check\": a command that reads the live "
        "state back after the change, \"files\": the files in the home folder the command changes}. Your sandbox "
        "cannot reach the running desktop, so %1 runs the check for you and tells you what it read. Prefer commands "
        "that apply a setting live, and after kwriteconfig6 tell the program to reload; change the desktop's look "
        "only with plasma-apply-colorscheme or plasma-apply-lookandfeel, using names installed on this computer. The "
        "person sees the change on a card and decides; %1 makes it outside your sandbox and tells you how it went. "
        "For upkeep, %1 has Helpers of its own, which look first, take restore points and show progress: propose "
        "{\"title\": a short instruction, \"card\": one of update (update the whole computer), space (free up "
        "space), install (install an app, with \"query\": what the person needs, such as \"pdf editor\"), restore "
        "(go back to a restore point), health (a health check), community (update apps from the AUR), tidy (tidy up "
        "the Downloads folder) or changes (what changed on this computer lately, each with its way back)} instead of "
        "a command for those. A change only the administrator can make, other than those, is one %1 can't make: say "
        "so in plain words and offer no card. Propose one change at a time, with a short, specific command that "
        "finishes on its own; to open a window, start it with kstart so the command returns at once. Check names on "
        "this computer rather than from memory, because Plasma 5 and 6 differ. Your sandbox cannot see the person's "
        "running programs, windows or screen, nor their files, so never say what a window shows; ask the person "
        "instead. Never ask for a password, a card number, bank details or a code sent by text or email. If you are "
        "not sure, say you don't know.").arg(productTitle());
}

void Chat::begin(const QString &question)
{
    const QString asked = question.trimmed();
    if (m_phase != QLatin1String("idle") && m_phase != QLatin1String("problem")
        && m_phase != QLatin1String("unavailable")) {
        if (!asked.isEmpty())
            ask(asked);
        return;
    }
    m_onlySignIn = asked.isEmpty();
    if (!asked.isEmpty()) {
        m_turns.append(QVariantMap{{QStringLiteral("who"), QStringLiteral("you")}, {QStringLiteral("text"), asked}});
        Q_EMIT turnsChanged();
        m_waiting = asked;
    }
    if (!m_agent.unavailable.isEmpty()) {
        setPhase(QStringLiteral("unavailable"), m_agent.unavailable);
        return;
    }
    start();
}

bool Chat::writeStandIns()
{
    const QString bin = QDir(m_requests).filePath(QStringLiteral("bin"));
    if (!QDir().mkpath(bin))
        return false;
    // Hands the page, the last argument, to Split Rock, which opens it in the
    // person's browser while they are signing in, and otherwise never.
    const QByteArray opener =
        "#!/bin/sh\n"
        "for page; do :; done\n"
        "case \"$page\" in http://*|https://*) printf '%s\\n' \"$page\" >> \"$SPLIT_ROCK_OPEN_REQUESTS\" ;; esac\n"
        "exit 0\n";
    for (const QString &name : {QStringLiteral("xdg-open"), QStringLiteral("kde-open"), QStringLiteral("gio"),
                                QStringLiteral("www-browser"), QStringLiteral("x-www-browser"),
                                QStringLiteral("sensible-browser")}) {
        if (!writeScript(QDir(bin).filePath(name), opener))
            return false;
    }
    // The one way to propose a change. It hands the proposal to Split Rock,
    // which shows it on the Do it card; it changes nothing itself.
    const QByteArray proposer =
        "#!/bin/sh\n"
        "if [ $# -ne 1 ]; then\n"
        "  echo 'usage: split-rock-change <proposal as one JSON argument>' >&2\n"
        "  exit 2\n"
        "fi\n"
        "printf '%s' \"$1\" | base64 -w0 >> \"$SPLIT_ROCK_CHANGE_REQUESTS\" && echo >> \"$SPLIT_ROCK_CHANGE_REQUESTS\" || exit 1\n"
        "echo 'Proposed. The person decides on the Do it card; the change is made only if they tap Do it, and you will be told how it went.'\n";
    if (!writeScript(QDir(bin).filePath(QLatin1String(ChangeProposal::Program)), proposer))
        return false;
    for (const QString &requests : {QStringLiteral("open-requests"), QStringLiteral("change-requests")}) {
        QFile file(QDir(m_requests).filePath(requests));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
    }
    m_openRead = 0;
    m_changeRead = 0;
    return true;
}

void Chat::start()
{
    m_framed = false;
    m_signIn.reset();
    m_signInPage.clear();
    setPhase(QStringLiteral("starting"));

    // Anything left from an earlier run that ended without tidying up.
    removeLeftovers(m_agent);
    QDir().mkpath(m_agent.home);
    m_work = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/split-rock-codex-XXXXXX"));
    m_requests = QDir(m_agent.home).filePath(QStringLiteral(".split-rock"));
    if (!m_work->isValid() || !writeStandIns()) {
        setPhase(QStringLiteral("problem"), QStringLiteral("Codex couldn't be given a folder of its own."));
        return;
    }

    acp::LaunchSpec launch = serveLaunch(m_agent, QProcessEnvironment::systemEnvironment(), m_work->path());
    const QString bin = QDir(m_requests).filePath(QStringLiteral("bin"));
    // Every way to open a page finds a stand-in first: the browser variable,
    // xdg-open, and the desktop openers behind it.
    launch.environment.insert(QStringLiteral("PATH"), bin + QLatin1Char(':')
                              + launch.environment.value(QStringLiteral("PATH")));
    launch.environment.insert(QStringLiteral("BROWSER"), QDir(bin).filePath(QStringLiteral("xdg-open")));
    launch.environment.insert(QStringLiteral("SPLIT_ROCK_OPEN_REQUESTS"), QDir(m_requests).filePath(QStringLiteral("open-requests")));
    launch.environment.insert(QStringLiteral("SPLIT_ROCK_CHANGE_REQUESTS"), QDir(m_requests).filePath(QStringLiteral("change-requests")));
    const QString fence = m_fence;
    if (!fence.isEmpty()) {
        ask::FenceRoom room;
        room.readable = m_agent.readable;
        const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        for (const QString &setting : DesktopSettings)
            room.readable.append(QDir(config).filePath(setting));
        room.writable = {m_agent.home, m_work->path()};
        launch.arguments = ask::fenced(launch.program, launch.arguments, room);
        launch.program = fence;
    } else if (!m_agent.mayRunUnfenced) {
        setPhase(QStringLiteral("unavailable"),
                 QStringLiteral("Codex can't be kept to its own folder on this computer, which lacks the sandbox it "
                                "runs in (bubblewrap)."));
        return;
    }

    m_client = new acp::Client(this);
    connect(m_client, &acp::Client::stopped, this, [this](const QString &, bool expected) {
        if (!expected)
            setPhase(QStringLiteral("problem"), QStringLiteral("Codex stopped."));
    });
    connect(m_client, &acp::Client::messageChunk, this, &Chat::say);
    connect(m_client, &acp::Client::toolCallChanged, this, [this](const acp::ToolCall &call) {
        // What it says after looking something up starts a paragraph of its
        // own.
        if (call.status == QLatin1String("completed"))
            m_answering = false;
    });
    connect(m_client, &acp::Client::changeRefused, this, [this](const acp::ToolCall &call, const QString &) {
        // A proposal through the stand-in that Codex asked leave to run: the
        // proposal itself goes on the card.
        if (std::optional<ChangeProposal> proposed = proposalIn(call)) {
            m_refused.insert(call.id);
            propose(*proposed);
            return;
        }
        if (m_refused.contains(call.id))
            return;
        m_refused.insert(call.id);
        // Told with the next question, so it proposes instead.
        m_aside = QStringLiteral("You asked leave to run \"%1\" yourself, and it was refused: nothing you run "
                                 "yourself changes this computer. Propose changes with split-rock-change.")
                      .arg(call.title.isEmpty() ? call.command() : call.title);
    });
    m_poll.start();
    m_client->start(launch);
    m_client->initialize(QStringLiteral(PROJECT_VERSION), productTitle(), [this](const std::optional<acp::RpcError> &error) {
        if (error) {
            setPhase(QStringLiteral("problem"), QStringLiteral("Codex didn't start: %1").arg(error->said()));
            return;
        }
        startSession();
    });
}

void Chat::startSession()
{
    if (!m_client)
        return;
    m_client->newSession(m_work->path(), [this](const std::optional<acp::RpcError> &error) {
        if (error && error->code == acp::AuthRequiredCode) {
            enterSignIn();
            return;
        }
        if (error) {
            setPhase(QStringLiteral("problem"), QStringLiteral("Codex couldn't start a conversation: %1").arg(error->said()));
            return;
        }
        const auto ready = [this] {
            if (m_onlySignIn) {
                setPhase(QStringLiteral("signedIn"));
                return;
            }
            setPhase(QStringLiteral("ready"));
            if (!m_waiting.isEmpty())
                send(std::exchange(m_waiting, QString()));
        };
        bool offered = false;
        for (const acp::Client::SessionMode &mode : m_client->modes())
            offered = offered || mode.id == m_agent.askFirstMode;
        if (!offered || m_client->currentMode() == m_agent.askFirstMode) {
            ready();
            return;
        }
        // Refusing every request still holds; the mode only makes Codex ask
        // more often.
        m_client->setMode(m_agent.askFirstMode, [ready](const std::optional<acp::RpcError> &) { ready(); });
    });
}

void Chat::enterSignIn()
{
    const QList<acp::AuthMethod> offered = offeredSignIns(m_agent, m_client->authMethods());
    if (offered.isEmpty()) {
        setPhase(QStringLiteral("problem"),
                 QStringLiteral("Codex offers no way to sign in here that needs neither a terminal nor a key."));
        return;
    }
    // ChatGPT first: the plan the person already pays for. Its id is
    // "chatgpt" in Zed's adapter and "chat-gpt" in the one that replaced it.
    m_signIn = offered.first();
    for (const acp::AuthMethod &method : offered) {
        if (QString(method.id).remove(QLatin1Char('-')).contains(QLatin1String("chatgpt"), Qt::CaseInsensitive)) {
            m_signIn = method;
            break;
        }
    }
    setPhase(QStringLiteral("signIn"));
    // From Settings the tap on Sign in was the ask.
    if (m_onlySignIn)
        signIn();
}

void Chat::signIn()
{
    if (m_phase != QLatin1String("signIn") || !m_client || !m_signIn)
        return;
    m_signInPage.clear();
    setNotice({});
    setPhase(QStringLiteral("signingIn"));
    m_signInTimer.start(m_signInLimit);
    m_client->authenticate(m_signIn->id, [this](const std::optional<acp::RpcError> &error) {
        m_signInTimer.stop();
        readRequests();
        if (error) {
            setNotice(QStringLiteral("Signing in didn't finish. You can try again."));
            setPhase(QStringLiteral("signIn"));
            return;
        }
        startSession();
    });
}

void Chat::ask(const QString &text)
{
    const QString question = text.trimmed();
    if (question.isEmpty() || m_phase == QLatin1String("working"))
        return;
    m_turns.append(QVariantMap{{QStringLiteral("who"), QStringLiteral("you")}, {QStringLiteral("text"), question}});
    Q_EMIT turnsChanged();
    setNotice({});
    if (m_phase == QLatin1String("ready")) {
        send(question);
        return;
    }
    // Asked before Codex is ready: it goes as soon as it can.
    m_waiting = m_waiting.isEmpty() ? question : m_waiting + QLatin1Char('\n') + question;
    if (m_phase == QLatin1String("signedIn")) {
        m_onlySignIn = false;
        setPhase(QStringLiteral("ready"));
        send(std::exchange(m_waiting, QString()));
    }
}

void Chat::send(const QString &prompt)
{
    if (!m_client)
        return;
    setPhase(QStringLiteral("working"));
    m_answering = false;
    m_freshTurn = true;
    QJsonArray blocks;
    const auto block = [](const QString &text) {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
    };
    if (!m_framed) {
        blocks.append(block(framing() + QStringLiteral("\n\n") + Appearance::current().describe()));
        m_framed = true;
    }
    if (!m_aside.isEmpty())
        blocks.append(block(std::exchange(m_aside, QString())));
    blocks.append(block(prompt));
    m_client->prompt(blocks, [this, prompt](const QString &stopReason, const std::optional<acp::RpcError> &error) {
        m_answering = false;
        if (error && error->code == acp::AuthRequiredCode) {
            m_waiting = prompt;
            enterSignIn();
            return;
        }
        if (error && error->code == acp::AgentStoppedCode)
            return; // Told by the stop itself.
        setPhase(QStringLiteral("ready"));
        if (error)
            setNotice(QStringLiteral("Codex couldn't answer. %1").arg(error->said()));
        else if (stopReason == QLatin1String("refusal"))
            setNotice(QStringLiteral("Codex would not answer that."));
        else if (stopReason == QLatin1String("max_tokens") || stopReason == QLatin1String("max_turn_requests"))
            setNotice(QStringLiteral("The answer was cut short."));
        else if (stopReason == QLatin1String("cancelled"))
            setNotice(QStringLiteral("Stopped."));
        if (!m_report.isEmpty())
            send(std::exchange(m_report, QString()));
    });
}

void Chat::say(const QString &text)
{
    if (m_freshTurn || m_turns.isEmpty()) {
        m_turns.append(QVariantMap{{QStringLiteral("who"), QStringLiteral("codex")}, {QStringLiteral("text"), QString()}});
        m_freshTurn = false;
    } else if (!m_answering) {
        QVariantMap last = m_turns.last().toMap();
        if (!last.value(QStringLiteral("text")).toString().isEmpty())
            last.insert(QStringLiteral("text"), last.value(QStringLiteral("text")).toString() + QStringLiteral("\n\n"));
        m_turns.last() = last;
    }
    m_answering = true;
    QVariantMap last = m_turns.last().toMap();
    last.insert(QStringLiteral("text"), last.value(QStringLiteral("text")).toString() + text);
    m_turns.last() = last;
    Q_EMIT turnsChanged();
}

void Chat::stop()
{
    if (m_phase == QLatin1String("working") && m_client)
        m_client->cancel();
}

void Chat::readRequests()
{
    const auto lines = [](const QString &path, qint64 &read) {
        QList<QByteArray> found;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() < read)
            return found;
        file.seek(read);
        const QByteArray added = file.readAll();
        const qsizetype lastLine = added.lastIndexOf('\n');
        if (lastLine < 0)
            return found;
        read += lastLine + 1;
        for (const QByteArray &line : added.left(lastLine).split('\n')) {
            if (!line.trimmed().isEmpty())
                found.append(line.trimmed());
        }
        return found;
    };
    if (m_requests.isEmpty())
        return;
    for (const QByteArray &line : lines(QDir(m_requests).filePath(QStringLiteral("open-requests")), m_openRead)) {
        const QString page = QString::fromUtf8(line);
        // Only a sign-in opens a page: Codex reaches no browser at any other
        // time.
        if (m_phase != QLatin1String("signingIn")
            || (!page.startsWith(QLatin1String("https://")) && !page.startsWith(QLatin1String("http://"))))
            continue;
        m_signInPage = page;
        Q_EMIT phaseChanged();
        Q_EMIT openPage(page);
    }
    for (const QByteArray &line : lines(QDir(m_requests).filePath(QStringLiteral("change-requests")), m_changeRead)) {
        if (std::optional<ChangeProposal> change = ChangeProposal::fromJson(QByteArray::fromBase64(line)))
            propose(*change);
    }
}

void Chat::propose(const ChangeProposal &proposed)
{
    // A whole-system update is never made from a card; the Update Helper
    // opens instead.
    const QString updater = proposed.wholeSystemUpdate();
    if (!updater.isEmpty()) {
        const bool opened = m_hands.openCard && m_hands.openCard(QStringLiteral("update"), {});
        report(QStringLiteral("Your proposed change \"%1\" updates the whole system with %2, or fetches the package "
                              "lists without updating. %3 never runs either from a card; %4. Don't propose the update "
                              "again; tell the person in one or two plain sentences, and propose single changes only.")
                   .arg(proposed.title, updater, productTitle(),
                        opened ? QStringLiteral("it opened its own Check for updates Helper instead, where the person decides")
                               : QStringLiteral("its own Check for updates Helper couldn't open")));
        return;
    }
    // One of the Helpers, asked for by name.
    if (!proposed.card.isEmpty()) {
        const bool opened = ChangeProposal::cards().contains(proposed.card) && m_hands.openCard
                         && m_hands.openCard(proposed.card, proposed.query);
        report(opened ? QStringLiteral("%1 opened its own %2 Helper, as you asked. The person decides there. Tell the "
                                       "person in one plain sentence that it is open.").arg(productTitle(), proposed.card)
                      : QStringLiteral("%1 couldn't open its %2 Helper. Tell the person to try it from Search.")
                            .arg(productTitle(), proposed.card));
        return;
    }
    // No card runs a command as the administrator.
    const QString elevation = proposed.elevation();
    if (!elevation.isEmpty()) {
        report(QStringLiteral("Your proposed change \"%1\" needs the administrator (%2), so it was not shown to the "
                              "person. %3 never runs your command as the administrator. Changes to the system are made "
                              "only by its own Helpers: update, space, install, restore and community. Propose one of "
                              "those with \"card\" when it fits; otherwise tell the person in one or two plain sentences "
                              "that this is a change %3 can't make for them.")
                   .arg(proposed.title, elevation == QLatin1String("admin") ? QStringLiteral("\"admin\": true") : elevation,
                        productTitle()));
        return;
    }
    if (m_runner && m_runner->busy())
        return;
    const Appearance now = Appearance::current();
    ChangeProposal settled = proposed;
    settled.command = now.resolve(proposed.command);
    const QString unknown = now.unknownName(proposed.command);
    if (!unknown.isEmpty()) {
        report(QStringLiteral("Your proposed change \"%1\" names \"%2\", which is not installed on this computer, so "
                              "it was not shown to the person. Propose it again with an installed name, or tell the "
                              "person it isn't installed.\n%3").arg(proposed.title, unknown, now.describe()));
        return;
    }
    const ChangeProposal change = ChangeRunner::withKnownUndo(settled, now);
    m_change = change;
    m_changeId.clear();
    const bool wayBack = !change.undo.trimmed().isEmpty() || !change.files.isEmpty();
    m_proposal = {
        {QStringLiteral("title"), change.title},
        {QStringLiteral("what"), change.what},
        {QStringLiteral("why"), change.why},
        {QStringLiteral("undoWay"), wayBack ? QStringLiteral("Undo puts it back as it was")
                                            : QStringLiteral("There is no Undo for this change")},
        {QStringLiteral("state"), QStringLiteral("offered")},
        {QStringLiteral("canUndo"), false},
    };
    Q_EMIT proposalChanged();
}

void Chat::doIt()
{
    if (!m_change || !m_runner || m_runner->busy()
        || m_proposal.value(QStringLiteral("state")).toString() != QLatin1String("offered"))
        return;
    // Written down, with how to undo it, before anything runs.
    QString unrecorded;
    const auto writeDown = [this, &unrecorded](const ChangeRunner::Record &record) {
        if (!m_notebook)
            return true;
        return m_notebook->recordChange(record.id, record.proposal.title, record.proposal.why, record.undoWay(),
                                        QStringLiteral("Making"), &unrecorded);
    };
    m_changeId = m_runner->make(*m_change, writeDown);
    if (m_changeId.isEmpty()) {
        setNotice(unrecorded.isEmpty()
                      ? QStringLiteral("Nothing was changed: %1 can't make this change safely on this computer.").arg(productTitle())
                      : QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded));
        return;
    }
    m_proposal.insert(QStringLiteral("state"), QStringLiteral("making"));
    Q_EMIT proposalChanged();
}

void Chat::notNow()
{
    if (m_proposal.value(QStringLiteral("state")).toString() != QLatin1String("offered"))
        return;
    m_aside = QStringLiteral("The person chose not to make your proposed change \"%1\"; nothing was changed.")
                  .arg(m_change ? m_change->title : QString());
    m_change.reset();
    m_proposal.clear();
    Q_EMIT proposalChanged();
}

void Chat::undo()
{
    if (m_changeId.isEmpty() || !m_runner || !m_proposal.value(QStringLiteral("canUndo")).toBool())
        return;
    QString error;
    if (!m_runner->undo(m_changeId, &error)) {
        setNotice(error);
        return;
    }
    m_proposal.insert(QStringLiteral("state"), QStringLiteral("undoing"));
    m_proposal.insert(QStringLiteral("canUndo"), false);
    Q_EMIT proposalChanged();
}

bool Chat::keep()
{
    const QString text = transcript(m_turns);
    if (text.isEmpty() || m_kept)
        return false;
    QString title;
    for (const QVariant &turn : std::as_const(m_turns)) {
        if (turn.toMap().value(QStringLiteral("who")).toString() == QLatin1String("you")) {
            title = turn.toMap().value(QStringLiteral("text")).toString().simplified();
            break;
        }
    }
    QString error;
    if (!m_hands.keep || !m_hands.keep(title, text, &error)) {
        setNotice(error.isEmpty() ? QStringLiteral("It couldn't be kept.") : error);
        return false;
    }
    m_kept = true;
    Q_EMIT keptChanged();
    setNotice(QStringLiteral("Kept as a note."));
    return true;
}

void Chat::end()
{
    m_poll.stop();
    m_signInTimer.stop();
    if (acp::Client *client = m_client.data()) {
        m_client.clear();
        client->disconnect(this);
        client->kill();
        client->deleteLater();
    }
    m_work.reset();
    removeLeftovers(m_agent);
}

void Chat::report(const QString &text)
{
    if (m_phase == QLatin1String("ready"))
        send(text);
    else
        m_report = text;
}

void Chat::setPhase(const QString &phase, const QString &problem)
{
    if (m_phase == phase && m_problem == problem)
        return;
    m_phase = phase;
    m_problem = problem;
    Q_EMIT phaseChanged();
}

void Chat::setNotice(const QString &notice)
{
    if (m_notice == notice)
        return;
    m_notice = notice;
    Q_EMIT noticeChanged();
}

} // namespace splitrock::codex
