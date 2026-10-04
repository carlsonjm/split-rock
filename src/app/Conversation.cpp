// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Conversation.h"

#include "app/ChangeCard.h"
#include "app/Fence.h"
#include "app/NeverWritten.h"
#include "app/Notebook.h"
#include "app/Product.h"

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
        "Do not change anything on this computer. You may look, but anything that would change a setting, "
        "a file or the system must be asked for, and the person decides. The one place you may write is the "
        "person's notebook, following the rules in its ASSISTANT.md. "
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
        m_said += text;
        m_answer = AnswerText::parse(m_said);
        Q_EMIT answerChanged();
    });
    connect(client, &acp::Client::changeRefused, this, [this](const acp::ToolCall &call, const QString &) {
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
    m_client->authenticate(methodId, [this](const std::optional<acp::RpcError> &error) {
        if (error) {
            signInDidNotFinish(error->message);
            return;
        }
        startSession();
    });
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

void Conversation::showMeHow()
{
    if (m_phase != QLatin1String("ready"))
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
    m_answer = {};
    m_card.clear();
    m_cardOpen = false;
    m_kept = false;
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
        if (relative == QLatin1String("This computer.md") || relative == QLatin1String("ASSISTANT.md"))
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

QStringList Conversation::fixedProblems() const
{
    return m_notebook ? m_notebook->fixedProblems() : QStringList();
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
