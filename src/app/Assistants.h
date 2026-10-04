// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Connection.h"
#include "acp/Protocol.h"

#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace splitrock {

// One assistant Split Rock can offer, and how it starts.
struct Assistant {
    QString id;
    QString name;
    // What it costs, in plain words; shown before signing in.
    QString cost;

    // The program and the arguments every run of it gets. Sign-in that the
    // agent asks the client to run uses these plus the method's own arguments.
    QString program;
    QStringList arguments;
    // Arguments only for serving the protocol.
    QStringList serveArguments;
    // Set on top of the cleaned environment, for every run.
    QMap<QString, QString> environment;

    // The assistant's own folder: its sign-in and its settings, kept apart from
    // any copy of the same assistant the person uses elsewhere.
    QString home;
    // What the assistant writes about a conversation, relative to home, as
    // wildcard names. Removed whenever the assistant stops, because
    // conversations are not saved.
    QStringList conversationLeftovers;

    // Sign-in methods Split Rock never offers for this assistant, each with
    // why not.
    QMap<QString, QString> hiddenMethods;
    // The session mode in which the assistant asks before changing anything.
    QString askFirstMode;

    // Empty when the assistant can start; otherwise why not, in plain words.
    QString unavailable;
};

// How Split Rock treats one sign-in method an agent advertised.
struct SignInChoice {
    enum class How {
        // The agent opens its own window, usually the browser.
        Agent,
        // The agent asks for its program to be run for sign-in. Split Rock runs
        // it without a terminal; whether that is enough is what Milestone 0
        // checks live.
        WithoutTerminal,
        // Not offered: it needs a key Split Rock would have to hold, or a ruling
        // keeps it out.
        NotOffered,
    };

    acp::AuthMethod method;
    How how = How::NotOffered;
    QString whyNot;
};

QList<SignInChoice> classifySignIn(const Assistant &assistant, const QList<acp::AuthMethod> &methods);

// The three real assistants, with their adapters fetched into adaptersDir and
// their own folders under stateDir. The order is the order setup offers them.
QList<Assistant> realAssistants(const QString &adaptersDir, const QString &stateDir);

// The pretend assistant, following script, with its folder under stateDir.
Assistant pretendAssistant(const QString &program, const QString &script, const QString &stateDir);

// The person's environment without anything that would sign an assistant in
// behind their back or change how it behaves: keys, tokens, and the settings
// of any assistant the program was started from.
QProcessEnvironment cleanEnvironment(const QProcessEnvironment &base);

// How to start the assistant to serve the protocol in workingDirectory.
acp::LaunchSpec serveLaunch(const Assistant &assistant, const QProcessEnvironment &base, const QString &workingDirectory);

// How to start the assistant's own sign-in for method.
acp::LaunchSpec signInLaunch(const Assistant &assistant, const acp::AuthMethod &method,
                             const QProcessEnvironment &base, const QString &workingDirectory);

// Removes the assistant's conversation leftovers from its folder. Touches
// nothing outside that folder.
void removeConversationLeftovers(const Assistant &assistant);

} // namespace splitrock
