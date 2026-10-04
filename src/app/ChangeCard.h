// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Protocol.h"

#include <QVariantMap>

namespace splitrock {

// The Do it card for a change an assistant asked to make: what it would change,
// why, how it could be undone and whether it needs the password, in plain
// words, with the exact request behind Show details.
//
// In Milestone 0 every such request has already been refused when the card is
// shown, and Do it is unavailable.
struct ChangeCard {
    QString kind;
    QString heading;
    QString whatChanges;
    QString why;
    QString undo;
    QString password;
    QString details;

    // assistantSaid is what the assistant had said in this turn before it
    // asked; question is what the person asked.
    static ChangeCard describe(const acp::ToolCall &call, const QString &assistantName,
                               const QString &assistantSaid, const QString &question);
    QVariantMap toMap() const;

    // Why Do it is unavailable, in Milestone 0 for every change.
    static QString unavailable();
};

} // namespace splitrock
