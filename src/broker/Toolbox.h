// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "tools/Capabilities.h"

#include <QDBusConnection>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace splitrock::broker {

// The apps' tools, through the broker (docs/BROKER.md § Tools): the files
// they describe themselves in, the checks every call passes, and the risk
// rules.
//
// A read runs at once and is named in what comes back. A write never runs
// from a call: it comes back as a card for the client to show, and runs only
// when the client hands back the card's token after the person's tap. An
// outward tool, one that sends or publishes, is refused. Every call is
// checked against the app's file before the app hears of it, and leaves one
// line in the log that names the tool, the caller and how it went, never what
// was passed or read.
class Toolbox : public QObject
{
    Q_OBJECT

public:
    using Reply = std::function<void(const QVariantMap &result)>;

    Toolbox(const QStringList &folders, const QDBusConnection &bus, QObject *parent = nullptr);

    // Every tool the installed apps describe, read again, as maps.
    QVariantList tools();
    // Calls a tool for caller (ask, codex, or the app's own name). The reply
    // carries ok and, when not, problem in plain words; from a read, tool,
    // app, appName and result; from a write, card (true), title, what, tool,
    // app, appName, args and token.
    void call(const QString &tool, const QVariantMap &arguments, const QString &caller, const Reply &reply);
    // Runs the write a card stands for, once, after the person's tap.
    void run(const QString &token, const Reply &reply);

    // A card's title: the first words of what it writes.
    static QString cardTitle(const tools::Tool &tool, const QVariantList &positional);

    // How long a card waits for its tap.
    static constexpr int CardMinutes = 30;

Q_SIGNALS:
    // A tool was asked for, so the broker isn't idle.
    void used();

private:
    struct Card {
        tools::Tool tool;
        QVariantList positional;
        QString caller;
        QDateTime made;
    };

    void invoke(const tools::Tool &tool, const QVariantList &positional, const QString &caller, const QString &how,
                const Reply &reply);
    static void log(const QString &tool, const QString &caller, const QString &outcome);

    tools::Capabilities m_capabilities;
    QDBusConnection m_bus;
    QHash<QString, Card> m_cards;
};

} // namespace splitrock::broker
