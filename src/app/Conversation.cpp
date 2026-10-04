// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Conversation.h"

#include "app/ChangeCard.h"

#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace splitrock {

namespace {

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
        "You are helping through Jack, a desktop assistant on a Linux computer with the KDE Plasma desktop. "
        "The person has used Windows for years and is new to Linux. Answer in plain words, with no jargon, "
        "and name the Windows equivalent when it helps. "
        "Start with one short sentence, then give at most five short numbered steps when steps help. "
        "Do not change anything on this computer. You may look, but anything that would change a setting, "
        "a file or the system must be asked for, and the person decides. "
        "Never ask for a password, a card number, bank details or a code sent by text or email. "
        "If you are not sure, say you don't know.");
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

QStringList Conversation::suggestions() const
{
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
    m_choosing = false;
    Q_EMIT assistantChanged();
    Q_EMIT choosingChanged();
    clearAnswer();
    startAssistant();
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
        m_refused.insert(call.id);
        m_card = ChangeCard::describe(call, m_current.name, m_said, m_question).toMap();
        m_cardOpen = true;
        Q_EMIT cardChanged();
    });
    connect(client, &acp::Client::toolCallChanged, this, [this](const acp::ToolCall &call) {
        // A change that finished without being asked for is what the fence in
        // Milestone 1 exists to stop; until then it is reported, plainly.
        const bool changesFiles = call.kind == QLatin1String("edit") || call.kind == QLatin1String("delete")
                               || call.kind == QLatin1String("move");
        if (changesFiles && call.status == QLatin1String("completed")
            && !m_refused.contains(call.id) && !m_unasked.contains(call.id)) {
            m_unasked.insert(call.id);
            setNotice(QStringLiteral("%1 changed something without asking: %2")
                          .arg(m_current.name, call.title.isEmpty() ? call.kind : call.title));
        }
    });

    client->start(serveLaunch(m_current, QProcessEnvironment::systemEnvironment(), m_workDir->path()));
    client->initialize(clientVersion(), [this](const std::optional<acp::RpcError> &error) {
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
                                  "Jack would hold, so Jack can't offer it.").arg(m_current.name),
                   why.join(QLatin1Char('\n')));
        return;
    }
    if (error.reason() == QLatin1String("claude_subscription_not_supported"))
        // The adapter gives this reason whether or not a subscription is
        // signed in, so the words must hold either way.
        setNotice(QStringLiteral("%1 needs an Anthropic Console sign-in here. "
                                 "A Claude subscription can't be used in Jack.").arg(m_current.name));
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
    if (question.isEmpty() || m_phase != QLatin1String("ready") || !m_client)
        return;
    send(question, question);
}

void Conversation::send(const QString &prompt, const QString &shown)
{
    clearAnswer();
    m_question = shown;
    Q_EMIT answerChanged();
    setNotice({});
    setPhase(QStringLiteral("working"));

    QJsonArray blocks;
    if (!m_framed) {
        blocks.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                  {QStringLiteral("text"), framing()}});
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
    // The conversation ends here and is not saved: the assistant starts over
    // and its own record of the conversation is removed.
    stopAssistant();
    clearAnswer();
    startAssistant();
}

void Conversation::setPhase(const QString &phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    Q_EMIT phaseChanged();
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
    m_question.clear();
    m_said.clear();
    m_answer = {};
    m_card.clear();
    m_cardOpen = false;
    m_refused.clear();
    m_unasked.clear();
    Q_EMIT answerChanged();
    Q_EMIT cardChanged();
}

QString Conversation::recentDiagnostics() const
{
    return m_diagnostics.join(QLatin1Char('\n'));
}

} // namespace splitrock
