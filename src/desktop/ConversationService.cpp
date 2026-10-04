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
        {QStringLiteral("answer"), c.headline()},
        {QStringLiteral("steps"), c.steps()},
        {QStringLiteral("suggestions"), c.suggestions()},
        // Do it is offered only for a change the assistant asked for, and in
        // Milestone 0 never works: the reason says why.
        {QStringLiteral("canDoIt"), false},
        {QStringLiteral("doItReason"), change ? ChangeCard::unavailable() : QString()},
        // Keep this arrives with the notebook.
        {QStringLiteral("kept"), false},
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
    if (m_conversation->phase() != QLatin1String("ready") || m_conversation->choosing())
        return refuse(QLatin1String(NotReady), QStringLiteral("The assistant is not ready for a question."));
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
    if (action == QLatin1String("keep"))
        return refuse(QLatin1String(NotAvailable), QStringLiteral("Keeping answers arrives with the notebook."));
    if (action == QLatin1String("show-me-how")) {
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
