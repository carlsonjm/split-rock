// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/AnswerText.h"

#include <QRegularExpression>

namespace splitrock {

QString AnswerText::plain(const QString &markdown)
{
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\]\\([^)]*\\)"));
    static const QRegularExpression strong(QStringLiteral("(\\*\\*|__)(.+?)\\1"));
    static const QRegularExpression emphasis(QStringLiteral("(?<![\\w*])[*_]([^*_\\s][^*_]*?)[*_](?![\\w*])"));
    static const QRegularExpression heading(QStringLiteral("^#{1,6}\\s+"));
    QString text = markdown;
    text.replace(link, QStringLiteral("\\1"));
    text.replace(strong, QStringLiteral("\\2"));
    text.replace(emphasis, QStringLiteral("\\1"));
    text.remove(QLatin1Char('`'));
    text.replace(heading, QString());
    return text.simplified();
}

AnswerText AnswerText::parse(const QString &markdown)
{
    static const QRegularExpression item(QStringLiteral("^\\s{0,3}(?:\\d{1,2}[.)]|[-*+\\x{2022}])\\s+(.*)$"));
    static const QRegularExpression fence(QStringLiteral("^\\s*(```|~~~)"));
    static const QRegularExpression rule(QStringLiteral("^\\s*([-*_])\\s*(\\1\\s*){2,}$"));

    QStringList paragraphs;
    QStringList steps;
    QString current;
    bool inStep = false;
    // Paragraphs that came before the first step, so the first one can lead.
    int paragraphsBeforeSteps = -1;

    const auto flush = [&] {
        const QString text = plain(current);
        if (!text.isEmpty()) {
            if (inStep)
                steps.append(text);
            else
                paragraphs.append(text);
        }
        current.clear();
    };

    // The assistant offers to remember something on a line of its own; the
    // offer is shown as a question of Split Rock's, not as part of the answer.
    static const QRegularExpression offer(QStringLiteral("^\\s*[*_]*\\s*Remember that\\?\\s*[*_]*\\s*(.+)$"),
                                          QRegularExpression::CaseInsensitiveOption);
    QString remember;

    const QStringList lines = markdown.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QRegularExpressionMatch offered = offer.match(line);
        if (offered.hasMatch()) {
            flush();
            inStep = false;
            remember = plain(offered.captured(1));
            continue;
        }
        // A code block or a rule stands apart from the words around it.
        if (fence.match(line).hasMatch() || rule.match(line).hasMatch()) {
            flush();
            inStep = false;
            continue;
        }
        if (line.trimmed().isEmpty()) {
            flush();
            inStep = false;
            continue;
        }
        const QRegularExpressionMatch match = item.match(line);
        if (match.hasMatch()) {
            flush();
            if (paragraphsBeforeSteps < 0)
                paragraphsBeforeSteps = paragraphs.size();
            inStep = true;
            current = match.captured(1);
            continue;
        }
        if (inStep && !line.startsWith(QLatin1Char(' ')) && !line.startsWith(QLatin1Char('\t'))) {
            // An unindented line after a step starts ordinary text again.
            flush();
            inStep = false;
        }
        current += (current.isEmpty() ? QString() : QStringLiteral(" ")) + line.trimmed();
    }
    flush();

    AnswerText answer;
    if (paragraphsBeforeSteps != 0 && !paragraphs.isEmpty())
        answer.headline = paragraphs.takeFirst();
    answer.steps = steps;
    answer.more = paragraphs;
    answer.remember = remember;
    return answer;
}

} // namespace splitrock
