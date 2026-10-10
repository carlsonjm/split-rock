// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QJsonObject>
#include <QStringList>

#include <optional>

namespace splitrock {

// A change the assistant proposes, in its own words: what it is, why, the
// command that makes it, and how to put it back. The assistant proposes it by
// running the `split-rock-change` stand-in inside its fence with the proposal
// as one JSON argument; Split Rock shows it on the Do it card and makes it only
// when the person taps Do it.
struct ChangeProposal {
    QString title;
    QString what;
    QString why;
    // Run with /bin/sh -c, as the person, or as the administrator when admin
    // is set.
    QString command;
    // Run the same way to put the change back. Empty when the assistant knows
    // none.
    QString undo;
    // Run as the person after the change, to read the live state back for the
    // assistant, which can't reach the desktop from its sandbox.
    QString check;
    bool admin = false;
    // The person's own files the command changes: copied aside first, and put
    // back by Undo.
    QStringList files;
    // One of Split Rock's own upkeep cards to open instead of a command:
    // update, space, install, restore, health, community or changes. query
    // is what the person asked to install, for install.
    QString card;
    QString query;

    static constexpr const char *Program = "split-rock-change";
    static QStringList cards();

    // The proposal as the stand-in received it.
    static std::optional<ChangeProposal> fromJson(const QByteArray &json, QString *error = nullptr);
    // The proposal inside a command line that runs the stand-in, such as an
    // assistant's request to run `bash -lc "split-rock-change '{...}'"`.
    static std::optional<ChangeProposal> fromCommandLine(const QString &command, QString *error = nullptr);
    QJsonObject toJson() const;

    // The package tool in the command when it updates the whole system, or
    // fetches the package lists without updating, which leaves an Arch system
    // half updated; empty when it does neither. Neither is made from a card
    // (docs/DECISIONS.md, A whole-system update is not made from a card).
    QString wholeSystemUpdate() const;
};

// Splits a command line into words as a POSIX shell does for plain words and
// single- and double-quoted ones. No expansion is done.
QStringList shellWords(const QString &line);

} // namespace splitrock
