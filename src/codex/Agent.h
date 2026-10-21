// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Connection.h"
#include "acp/Protocol.h"

#include <QList>
#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace splitrock::codex {

// Codex, through its Agent Client Protocol adapter, and how it starts
// (docs/ASK.md, Codex). Tests use the pretend assistant in its place.
struct Agent {
    QString name;
    // The program and the arguments every run of it gets.
    QString program;
    QStringList arguments;
    // Set on top of the fenced environment, for every run.
    QMap<QString, QString> environment;
    // Its own folder: its sign-in and its settings, apart from any Codex the
    // person uses elsewhere.
    QString home;
    // What it writes about a conversation, relative to home, as wildcard
    // names. Removed whenever it stops, since conversations are not saved.
    QStringList leftovers;
    // The file whose presence says it is signed in, relative to home.
    QString signInFile;
    // Sign-in methods never offered, since each needs a key Split Rock
    // would hold.
    QStringList hiddenMethods;
    // The session mode in which it asks before changing anything.
    QString askFirstMode;
    // What the fence must still let it read inside the home folder.
    QStringList readable;
    // Only the pretend assistant, which reaches nothing, may run where the
    // computer has no fence to put it in.
    bool mayRunUnfenced = false;
    // Empty when it can start; otherwise why not, in plain words.
    QString unavailable;

    bool signedIn() const;
    // Forgets the sign-in: Codex's own file, and what a conversation left.
    bool signOut() const;
};

// Codex's adapter: Shuffle's own copy first, then one on the PATH; empty
// when there is none.
QString findAdapter();
// Its folder, in Split Rock's own data folder.
QString defaultHome();

Agent codex(const QString &adapter = findAdapter(), const QString &home = defaultHome());
// The pretend assistant, following script, with its folder at home.
Agent pretend(const QString &program, const QString &script, const QString &home);

// The sign-ins the person is offered: those the agent runs itself, in the
// browser. One that needs a key, or a terminal, never is.
QList<acp::AuthMethod> offeredSignIns(const Agent &agent, const QList<acp::AuthMethod> &methods);

// How to start it to serve the protocol in work, before the fence.
acp::LaunchSpec serveLaunch(const Agent &agent, const QProcessEnvironment &base, const QString &work);

// Removes what a conversation left in its folder. Touches nothing outside
// that folder.
void removeLeftovers(const Agent &agent);

} // namespace splitrock::codex
