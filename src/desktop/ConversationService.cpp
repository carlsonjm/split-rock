// SPDX-License-Identifier: GPL-2.0-or-later
#include "desktop/ConversationService.h"

#include "app/ChangeCard.h"
#include "app/Conversation.h"
#include "desktop/Shell.h"

#include <QDBusError>

namespace splitrock {

namespace {

constexpr char NotReady[] = "io.github.carlsonjm.SplitRock.Error.NotReady";
constexpr char NotAvailable[] = "io.github.carlsonjm.SplitRock.Error.NotAvailable";
constexpr char Refused[] = "io.github.carlsonjm.SplitRock.Error.Refused";

// The conversation's phases as the interface names them.
QString publicPhase(const Conversation &c)
{
    const QString phase = c.phase();
    if (c.choosing() || phase == QLatin1String("choose"))
        return QStringLiteral("choose");
    if (phase == QLatin1String("signIn") || phase == QLatin1String("signingIn"))
        return QStringLiteral("sign-in");
    if (phase == QLatin1String("working"))
        return QStringLiteral("answering");
    if (phase == QLatin1String("problem"))
        return QStringLiteral("failed");
    return phase; // starting, ready
}

} // namespace

ConversationService::ConversationService(Conversation *conversation, Shell *shell, QObject *parent)
    : QObject(parent)
    , m_conversation(conversation)
    , m_shell(shell)
{
    m_changed.setSingleShot(true);
    m_changed.setInterval(0);
    connect(&m_changed, &QTimer::timeout, this, &ConversationService::changedElsewhere);
    for (auto signal : {&Conversation::choosingChanged, &Conversation::assistantChanged, &Conversation::phaseChanged,
                        &Conversation::answerChanged, &Conversation::cardChanged, &Conversation::noticeChanged}) {
        connect(conversation, signal, &m_changed, qOverload<>(&QTimer::start));
    }
    connect(shell, &Shell::windowShown, this, &ConversationService::WindowShown);
}

bool ConversationService::publish(QDBusConnection bus)
{
    return bus.registerObject(QLatin1String(Path), this, QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals);
}

QVariantMap ConversationService::state() const
{
    const Conversation &c = *m_conversation;
    const bool change = !c.card().isEmpty();
    return {
        {QStringLiteral("phase"), publicPhase(c)},
        {QStringLiteral("assistant"), c.assistantName()},
        {QStringLiteral("question"), c.question()},
        // An answer that opens with its steps has no headline; its first
        // paragraph after them leads instead, and the rest follow in more.
        {QStringLiteral("answer"), c.headline().isEmpty() ? c.more().value(0) : c.headline()},
        {QStringLiteral("more"), c.headline().isEmpty() ? c.more().mid(1) : c.more()},
        {QStringLiteral("steps"), c.steps()},
        {QStringLiteral("suggestions"), c.suggestions()},
        // Do it is offered only for a change the assistant asked for, and in
        // Milestone 0 never works: the reason says why.
        {QStringLiteral("canDoIt"), false},
        {QStringLiteral("doItReason"), change ? ChangeCard::unavailable() : QString()},
        {QStringLiteral("kept"), c.kept()},
        // Added after version 1's first table; a version-1 caller may ignore
        // them: what the assistant offers to remember, until answered, and
        // whether That fixed it was used.
        {QStringLiteral("remember"), c.remember()},
        {QStringLiteral("fixed"), c.fixed()},
        {QStringLiteral("problem"), c.problem()},
    };
}

QVariantMap ConversationService::reply()
{
    m_sent = state();
    return m_sent;
}

QVariantMap ConversationService::refuse(const QString &error, const QString &message)
{
    if (calledFromDBus())
        sendErrorReply(error, message);
    return reply();
}

void ConversationService::changedElsewhere()
{
    const QVariantMap now = state();
    if (now == m_sent)
        return;
    m_sent = now;
    Q_EMIT Changed(now);
}

uint ConversationService::ProtocolVersion() const
{
    return Version;
}

QVariantMap ConversationService::Start()
{
    // The assistant the person chose last time starts again; with none, the
    // phase is choose, and the full window is where one is chosen.
    m_conversation->resumeChosen();
    return reply();
}

QVariantMap ConversationService::Ask(const QString &text)
{
    if (text.trimmed().isEmpty())
        return refuse(QDBusError::errorString(QDBusError::InvalidArgs), QStringLiteral("There is no question."));
    // A question asked while the assistant starts or is signed in waits for
    // it; only with no assistant chosen, or one that failed, is there nothing
    // to ask.
    const QString phase = m_conversation->phase();
    if (m_conversation->choosing() || phase == QLatin1String("choose") || phase == QLatin1String("problem"))
        return refuse(QLatin1String(NotReady), QStringLiteral("There is no assistant ready to ask."));
    if (phase == QLatin1String("working"))
        return refuse(QLatin1String(NotReady), QStringLiteral("An answer is still coming."));
    m_conversation->ask(text);
    return reply();
}

QVariantMap ConversationService::Cancel()
{
    m_conversation->stop();
    return reply();
}

QVariantMap ConversationService::Act(const QString &action)
{
    if (action == QLatin1String("keep")) {
        if (!m_conversation->canKeep())
            return refuse(QLatin1String(NotAvailable), QStringLiteral("There is no notebook to keep answers in."));
        if (m_conversation->phase() != QLatin1String("ready") || m_conversation->question().isEmpty())
            return refuse(QLatin1String(NotReady), QStringLiteral("There is no answer to keep yet."));
        // Refused for what it holds: the reason says why, as the notice does.
        if (!m_conversation->keep())
            return refuse(QLatin1String(Refused), m_conversation->notice());
        return reply();
    }
    if (action == QLatin1String("remember") || action == QLatin1String("dont-remember")) {
        if (m_conversation->remember().isEmpty())
            return refuse(QLatin1String(NotAvailable), QStringLiteral("Nothing is offered to remember."));
        if (action == QLatin1String("dont-remember")) {
            m_conversation->rememberNo();
            return reply();
        }
        if (!m_conversation->rememberYes())
            return refuse(QLatin1String(Refused), m_conversation->notice());
        return reply();
    }
    if (action == QLatin1String("fixed")) {
        if (!m_conversation->canKeep())
            return refuse(QLatin1String(NotAvailable), QStringLiteral("There is no notebook to record it in."));
        if (m_conversation->phase() != QLatin1String("ready") || m_conversation->question().isEmpty())
            return refuse(QLatin1String(NotReady), QStringLiteral("There is no answer yet."));
        if (!m_conversation->markFixed())
            return refuse(QLatin1String(Refused), m_conversation->notice());
        return reply();
    }
    if (action == QLatin1String("show-me-how")) {
        if (m_conversation->isGuide())
            return refuse(QLatin1String(NotAvailable),
                          QStringLiteral("The guide answers from what is written; Show me how needs an assistant."));
        if (m_conversation->phase() != QLatin1String("ready") || m_conversation->question().isEmpty())
            return refuse(QLatin1String(NotReady), QStringLiteral("There is no answer to show how to do."));
        m_conversation->showMeHow();
        return reply();
    }
    if (action == QLatin1String("do-it")) {
        if (m_conversation->card().isEmpty())
            return refuse(QLatin1String(NotAvailable), QStringLiteral("This answer asks to change nothing."));
        // The card is decided on in the full window, as Do it is never done
        // from a search.
        m_conversation->openCard();
        m_shell->openFull(QString());
        return reply();
    }
    return refuse(QDBusError::errorString(QDBusError::InvalidArgs),
                  QStringLiteral("Unknown action: %1").arg(action));
}

bool ConversationService::OpenWindow(const QString &requestToken)
{
    m_shell->openFull(requestToken);
    return true;
}

} // namespace splitrock
