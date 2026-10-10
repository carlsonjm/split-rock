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

QStringList ChangeProposal::cards()
{
    return {QStringLiteral("update"), QStringLiteral("space"), QStringLiteral("install"),
            QStringLiteral("restore"), QStringLiteral("health"), QStringLiteral("community"), QStringLiteral("changes")};
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
    change.card = object.value(QStringLiteral("card")).toString().trimmed().toLower();
    change.query = object.value(QStringLiteral("query")).toString().simplified().left(80);
    if (!change.card.isEmpty() && !cards().contains(change.card))
        return fail(QStringLiteral("There is no %1 card. The cards are: %2.").arg(change.card, cards().join(QStringLiteral(", "))));
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
    if (change.command.isEmpty() && change.card.isEmpty())
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

namespace {

// The package tool in a command line that updates the whole system or only
// fetches the package lists, with any shell -c string inside it searched in
// turn.
QString wholeSystemUpdateIn(const QString &line, int depth)
{
    static const QStringList tools{QStringLiteral("pacman"), QStringLiteral("paru"), QStringLiteral("yay")};
    const QStringList words = shellWords(line);
    for (qsizetype i = 0; i < words.size(); ++i) {
        const QString &word = words.at(i);
        if (depth < 3 && word.contains(QLatin1Char(' '))) {
            const QString inside = wholeSystemUpdateIn(word, depth + 1);
            if (!inside.isEmpty())
                return inside;
        }
        const QString tool = word.section(QLatin1Char('/'), -1);
        if (!tools.contains(tool))
            continue;
        // The tool's own words, up to the end of its command.
        bool sync = false;
        bool refresh = false;
        bool upgrade = false;
        bool operation = false;
        for (qsizetype j = i + 1; j < words.size(); ++j) {
            const QString &arg = words.at(j);
            if (arg == QLatin1String(";") || arg == QLatin1String("&") || arg == QLatin1String("|"))
                break;
            if (arg.startsWith(QLatin1String("--"))) {
                sync = sync || arg == QLatin1String("--sync");
                refresh = refresh || arg == QLatin1String("--refresh");
                upgrade = upgrade || arg == QLatin1String("--sysupgrade");
                operation = operation || arg == QLatin1String("--sync") || arg == QLatin1String("--query")
                    || arg == QLatin1String("--remove") || arg == QLatin1String("--upgrade")
                    || arg == QLatin1String("--database") || arg == QLatin1String("--files")
                    || arg == QLatin1String("--deptest") || arg == QLatin1String("--getpkgbuild")
                    || arg == QLatin1String("--show");
            } else if (arg.startsWith(QLatin1Char('-')) && arg.size() > 1) {
                const QStringView letters = QStringView(arg).mid(1);
                for (const QChar letter : letters) {
                    if (QStringLiteral("SQRUDFTGPWBCY").contains(letter))
                        operation = true;
                }
                sync = sync || letters.contains(QLatin1Char('S'));
                refresh = refresh || letters.contains(QLatin1Char('y'));
                upgrade = upgrade || letters.contains(QLatin1Char('u'));
            } else if (tool != QLatin1String("pacman")) {
                // paru or yay given a name searches for it.
                operation = true;
            }
        }
        // paru or yay on its own updates the whole system.
        if (!operation && tool != QLatin1String("pacman"))
            return tool;
        if (sync && (upgrade || refresh))
            return tool;
    }
    return {};
}

} // namespace

QString ChangeProposal::wholeSystemUpdate() const
{
    return wholeSystemUpdateIn(command, 0);
}

QJsonObject ChangeProposal::toJson() const
{
    QJsonObject object{{QStringLiteral("title"), title},
            {QStringLiteral("what"), what},
            {QStringLiteral("why"), why},
            {QStringLiteral("command"), command},
            {QStringLiteral("undo"), undo},
            {QStringLiteral("check"), check},
            {QStringLiteral("admin"), admin},
            {QStringLiteral("files"), QJsonArray::fromStringList(files)}};
    if (!card.isEmpty())
        object.insert(QStringLiteral("card"), card);
    if (!query.isEmpty())
        object.insert(QStringLiteral("query"), query);
    return object;
}

} // namespace splitrock
