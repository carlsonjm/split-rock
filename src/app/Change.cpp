// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Change.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

namespace splitrock {

QStringList shellWords(const QString &line)
{
    QStringList words;
    QString word;
    bool inWord = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('\'')) {
            inWord = true;
            const qsizetype end = line.indexOf(QLatin1Char('\''), i + 1);
            if (end < 0) {
                word += line.mid(i + 1);
                i = line.size();
            } else {
                word += line.mid(i + 1, end - i - 1);
                i = end;
            }
        } else if (c == QLatin1Char('"')) {
            inWord = true;
            for (++i; i < line.size() && line.at(i) != QLatin1Char('"'); ++i) {
                if (line.at(i) == QLatin1Char('\\') && i + 1 < line.size()
                    && QStringLiteral("\"\\$`\n").contains(line.at(i + 1)))
                    ++i;
                word += line.at(i);
            }
        } else if (c == QLatin1Char('\\') && i + 1 < line.size()) {
            inWord = true;
            word += line.at(++i);
        } else if (c.isSpace()) {
            if (inWord)
                words.append(std::exchange(word, QString()));
            inWord = false;
        } else if (!inWord && (c == QLatin1Char(';') || c == QLatin1Char('&') || c == QLatin1Char('|'))) {
            words.append(QString(c));
        } else {
            inWord = true;
            word += c;
        }
    }
    if (inWord)
        words.append(word);
    return words;
}

std::optional<ChangeProposal> ChangeProposal::fromJson(const QByteArray &json, QString *error)
{
    const auto fail = [error](const QString &why) -> std::optional<ChangeProposal> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parse);
    if (!document.isObject())
        return fail(QStringLiteral("The proposal is not a JSON object."));
    const QJsonObject object = document.object();
    ChangeProposal change;
    change.title = object.value(QStringLiteral("title")).toString().simplified();
    change.what = object.value(QStringLiteral("what")).toString().simplified();
    change.why = object.value(QStringLiteral("why")).toString().simplified();
    change.command = object.value(QStringLiteral("command")).toString().trimmed();
    change.undo = object.value(QStringLiteral("undo")).toString().trimmed();
    change.check = object.value(QStringLiteral("check")).toString().trimmed();
    change.admin = object.value(QStringLiteral("admin")).toBool();
    for (const QJsonValue &file : object.value(QStringLiteral("files")).toArray()) {
        const QString path = file.toString().trimmed();
        if (!path.isEmpty())
            change.files.append(path);
    }
    // Administrator rights come only through Split Rock's own prompt.
    static const QRegularExpression elevated(QStringLiteral("^(sudo|pkexec|doas|run0)\\s+"));
    if (elevated.match(change.command).hasMatch()) {
        change.command.remove(elevated);
        change.admin = true;
    }
    if (elevated.match(change.undo).hasMatch())
        change.undo.remove(elevated);
    if (change.title.isEmpty())
        return fail(QStringLiteral("The proposal has no title."));
    if (change.command.isEmpty())
        return fail(QStringLiteral("The proposal has no command."));
    if (change.what.isEmpty())
        change.what = change.title;
    return change;
}

std::optional<ChangeProposal> ChangeProposal::fromCommandLine(const QString &command, QString *error)
{
    const QLatin1String program(Program);
    // The stand-in may be wrapped in a shell's -c string, so its words are
    // searched for at every level down.
    QString line = command;
    for (int depth = 0; depth < 3; ++depth) {
        const QStringList words = shellWords(line);
        const qsizetype at = words.indexOf(program);
        if (at >= 0 && at + 1 < words.size())
            return fromJson(words.at(at + 1).toUtf8(), error);
        if (at >= 0)
            break;
        const qsizetype dashC = words.indexOf(QStringLiteral("-c")) >= 0 ? words.indexOf(QStringLiteral("-c"))
                                                                         : words.indexOf(QStringLiteral("-lc"));
        if (dashC < 0 || dashC + 1 >= words.size())
            break;
        line = words.at(dashC + 1);
    }
    if (error)
        *error = QStringLiteral("No proposal was given to %1.").arg(program);
    return std::nullopt;
}

QJsonObject ChangeProposal::toJson() const
{
    return {{QStringLiteral("title"), title},
            {QStringLiteral("what"), what},
            {QStringLiteral("why"), why},
            {QStringLiteral("command"), command},
            {QStringLiteral("undo"), undo},
            {QStringLiteral("check"), check},
            {QStringLiteral("admin"), admin},
            {QStringLiteral("files"), QJsonArray::fromStringList(files)}};
}

} // namespace splitrock
