// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace splitrock {

// An answer laid out as the assistant's window shows it: one short line, then
// numbered steps, then anything else the assistant said.
struct AnswerText {
    QString headline;
    QStringList steps;
    QStringList more;

    // Reads an assistant's Markdown, which may be cut off mid-way while it is
    // still arriving, and keeps only plain words.
    static AnswerText parse(const QString &markdown);
    // Inline Markdown reduced to plain words.
    static QString plain(const QString &markdown);
};

} // namespace splitrock
