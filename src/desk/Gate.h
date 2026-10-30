// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace splitrock::desk {

// The desk's own safety check, for the assistant on this computer. It asks no
// model: a fixed list of patterns catches the asks a small model must never
// answer itself, and each kind has the desk's own fixed answer. The model is also
// asked whether an ask is unsafe, and names a kind from this same list; either
// way, the words the person reads are these.
class Gate
{
public:
    // The kinds, by name: scam, remote, money, password, destructive,
    // piracy, spying, updates, scareware and protection.
    static QStringList kinds();

    // The kind of unsafe ask this is, if the patterns catch it.
    static std::optional<QString> check(const QString &ask);

    // Whether the patterns catch it as harm to the person or someone else,
    // rather than only a password or a key of their own.
    static bool harmful(const QString &ask);

    // The desk's fixed answer for a kind. Empty for a kind it doesn't know.
    static QString answer(const QString &kind);
};

} // namespace splitrock::desk
