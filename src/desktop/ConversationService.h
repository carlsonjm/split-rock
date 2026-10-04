// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace splitrock {

class Conversation;
class Shell;

// The conversation, offered on the session bus to a desktop search that draws
// it in its own window (docs/DESKTOP.md § The conversation in Search). The
// search keeps nothing: every method returns the conversation as Split Rock
// keeps it, and Changed tells it of every other change, an answer as it
// streams included.
class ConversationService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.carlsonjm.SplitRock.Conversation")

public:
    static constexpr uint Version = 1;
    static constexpr char Service[] = "io.github.carlsonjm.splitrock";
    static constexpr char Path[] = "/Conversation";

    ConversationService(Conversation *conversation, Shell *shell, QObject *parent = nullptr);

    // Registers the object on the bus; the name is the program's to claim.
    bool publish(QDBusConnection bus);

    // The conversation as the interface gives it.
    QVariantMap state() const;

public Q_SLOTS:
    Q_SCRIPTABLE uint ProtocolVersion() const;
    Q_SCRIPTABLE QVariantMap Start();
    Q_SCRIPTABLE QVariantMap Ask(const QString &text);
    Q_SCRIPTABLE QVariantMap Cancel();
    Q_SCRIPTABLE QVariantMap Act(const QString &action);
    Q_SCRIPTABLE bool OpenWindow(const QString &requestToken);

Q_SIGNALS:
    Q_SCRIPTABLE void Changed(const QVariantMap &state);
    Q_SCRIPTABLE void WindowShown(const QString &requestToken);

private:
    // A call's reply is the state it leaves; Changed is sent only for what
    // differs from what the search last had.
    QVariantMap reply();
    QVariantMap refuse(const QString &error, const QString &message);
    void changedElsewhere();

    Conversation *m_conversation;
    Shell *m_shell;
    QVariantMap m_sent;
    // One change can move the phase, the answer and the card at once; it is
    // told once.
    QTimer m_changed;
};

} // namespace splitrock
