// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Conversation.h"

#include "app/ChangeCard.h"
#include "app/ChangeRunner.h"
#include "app/Fence.h"
#include "app/NeverWritten.h"
#include "app/Notebook.h"
#include "app/Product.h"

#include <QDate>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace splitrock {

namespace {

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
        m_report = QStringLiteral("The person tapped Do it, and %1 ran your proposed change \"%2\" as %3. Exit code %4. "
                                  "Output:\n%5\n\n%6\n\nTell the person in plain words how it went. You cannot see their "
                                  "screen or running programs, so when the change is something only they can see, ask them "
                                  "whether it worked. Don't propose the same change again unless it failed and you know why.")
                       .arg(productName(), record.proposal.title,
                            record.proposal.admin ? QStringLiteral("the administrator") : QStringLiteral("the person"))
                       .arg(record.exitCode)
                       .arg(output.isEmpty() ? QStringLiteral("(none)") : output, checked);
        m_reportShown = ok ? QStringLiteral("Did it: %1").arg(record.proposal.title)
                           : QStringLiteral("It didn't work: %1").arg(record.proposal.title);
        if (m_client && m_phase == QLatin1String("ready"))
            send(std::exchange(m_report, QString()), std::exchange(m_reportShown, QString()));
    });
    connect(m_runner, &ChangeRunner::undone, this, [this](const ChangeRunner::Record &record) {
        const bool ok = record.status == QLatin1String("undone");
        if (m_notebook) {
            m_notebook->markChange(record.id, ok ? QStringLiteral("Undone %1").arg(QDate::currentDate().toString(Qt::ISODate))
                                                 : QStringLiteral("Undo failed"));
            Q_EMIT notebookChanged();
        }
        Q_EMIT changesChanged();
        setNotice(ok ? QStringLiteral("Undone: %1.").arg(record.proposal.title)
                     : QStringLiteral("The undo didn't work: %1.").arg(record.proposal.title));
    });
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
        "\"why\": why, in plain words, \"command\": the shell command that makes the change, \"admin\": true "
        "when it needs administrator rights (never put sudo or pkexec in the command), \"undo\": a command that "
        "puts it back, when there is one, applied live the same way, \"check\": a command that reads the live state "
        "back after the change, such as a qdbus6 call to the program the setting belongs to, \"files\": the files "
        "in the home folder the command changes}. Your sandbox cannot reach the running desktop, so %1 runs the "
        "check for you and shows you what it read; reading a settings file back does not show the desktop "
        "changed. Prefer commands that apply a setting live, such as plasma-apply-colorscheme or "
        "plasma-apply-lookandfeel, and after kwriteconfig6 tell the program to reload. The "
        "person sees it on a card and decides; %1 makes the change outside your sandbox and tells you how it went. "
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
    if (!m_guide.entries(m_guideContext, GuideEntry::Kind::Guide).isEmpty()) {
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

void Conversation::chooseAssistant(const QString &id)
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
    startAssistant();
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
    client->setAllow([this](const acp::ToolCall &call) { return mayWrite(call); });
    connect(client, &acp::Client::requestAllowed, this, [this](const acp::ToolCall &call) {
        m_allowed.insert(call.id);
        QStringList written;
        for (const QString &path : call.paths + call.diffPaths)
            written.append(QDir(m_notebook->folder()).relativeFilePath(path));
        written.removeDuplicates();
        setNotice(QStringLiteral("%1 wrote in the notebook: %2").arg(m_current.name, written.join(QStringLiteral(", "))));
        Q_EMIT notebookChanged();
    });

    acp::LaunchSpec launch = serveLaunch(m_current, QProcessEnvironment::systemEnvironment(), m_workDir->path());
    if (!fenceProgram().isEmpty()) {
        FenceRoom room;
        room.writable = {m_current.home, m_workDir->path()};
        if (m_notebook)
            room.writable.append(m_notebook->folder());
        room.readable = m_current.readable;
        if (!m_current.home.isEmpty()) {
            m_openRequests = QDir(m_current.home).filePath(QStringLiteral(".split-rock/open-requests"));
            QFile::remove(m_openRequests);
            m_openRequestsRead = 0;
            room.openRequests = m_openRequests;
            m_changeRequests = QDir(m_current.home).filePath(QStringLiteral(".split-rock/change-requests"));
            QFile::remove(m_changeRequests);
            m_changeRequestsRead = 0;
            room.changeRequests = m_changeRequests;
            if (!m_changeRequestsTimer) {
                m_changeRequestsTimer = new QTimer(this);
                m_changeRequestsTimer->setInterval(300);
                connect(m_changeRequestsTimer, &QTimer::timeout, this, &Conversation::readChangeRequests);
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
        && (m_phase == QLatin1String("starting") || m_phase == QLatin1String("signIn")
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
    if (m_current.guide) {
        answerFromGuide(question);
        return;
    }
    if (!m_client)
        return;
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

void Conversation::send(const QString &prompt, const QString &shown)
{
    if (!m_client)
        return;
    keepTurn();
    clearAnswer();
    m_question = shown;
    Q_EMIT answerChanged();
    setNotice({});
    setPhase(QStringLiteral("working"));

    QJsonArray blocks;
    if (!m_framed) {
        blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                  {QStringLiteral("text"), framing()}});
        // What the notebook remembers, as the conversation starts.
        const QString memory = m_notebook ? m_notebook->memory() : QString();
        if (!memory.isEmpty())
            blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), memory}});
        m_framed = true;
    }
    blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), prompt}});

    m_client->prompt(blocks, [this](const QString &stopReason, const std::optional<acp::RpcError> &error) {
        if (error && error->code == acp::AuthRequiredCode) {
            enterSignIn(*error);
            return;
        }
        if (error && error->code == acp::AgentStoppedCode)
            return; // Reported by the stop itself.
        setPhase(QStringLiteral("ready"));
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

void Conversation::propose(const ChangeProposal &change)
{
    m_proposal = change;
    m_card = ChangeCard::forProposal(change, m_current.name).toMap();
    m_cardOpen = true;
    Q_EMIT cardChanged();
}

void Conversation::doIt()
{
    if (!m_proposal || m_runner->busy())
        return;
    // Written down, with how to undo it, before anything runs.
    QString unrecorded;
    const auto writeDown = [this, &unrecorded](const ChangeRunner::Record &record) {
        if (!m_notebook)
            return true;
        const QString undo = record.proposal.admin && ChangeRunner::systemSnapshots()
            ? QStringLiteral("Put back the system files it changed, from the snapshot taken before")
            : record.undoWay();
        const bool written = m_notebook->recordChange(record.id, record.proposal.title, record.proposal.why, undo,
                                                      QStringLiteral("Making"), &unrecorded);
        Q_EMIT notebookChanged();
        return written;
    };
    if (m_runner->make(*m_proposal, writeDown).isEmpty()) {
        Q_EMIT changesChanged();
        if (!unrecorded.isEmpty())
            setNotice(QStringLiteral("Nothing was changed: it couldn't be written in the notebook first. %1").arg(unrecorded));
        return;
    }
    m_card.insert(QStringLiteral("making"), true);
    Q_EMIT cardChanged();
    Q_EMIT changesChanged();
    setNotice(m_proposal->admin ? QStringLiteral("Making the change. The system asks for your password.")
                                : QStringLiteral("Making the change."));
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
            send(std::exchange(m_report, QString()), std::exchange(m_reportShown, QString()));
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
