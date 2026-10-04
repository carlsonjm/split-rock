// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/ChangeCard.h"

#include "app/AnswerText.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace splitrock {

namespace {

QString shortName(const acp::ToolCall &call)
{
    const QString path = !call.paths.isEmpty() ? call.paths.first()
                       : !call.diffPaths.isEmpty() ? call.diffPaths.first() : QString();
    return path.isEmpty() ? QString() : QFileInfo(path).fileName();
}

// The last thing the assistant said before asking, as its reason.
QString lastSaid(const QString &said)
{
    const AnswerText text = AnswerText::parse(said);
    QString reason = !text.more.isEmpty() ? text.more.last()
                   : !text.steps.isEmpty() ? text.steps.last() : text.headline;
    if (reason.size() > 240)
        reason = reason.left(237).trimmed() + QStringLiteral("...");
    return reason;
}

bool needsPassword(const QString &command)
{
    static const QRegularExpression privileged(QStringLiteral("(^|[\\s;&|(])(sudo|pkexec|su|doas|run0)(\\s|$)"));
    return privileged.match(command).hasMatch();
}

} // namespace

ChangeCard ChangeCard::describe(const acp::ToolCall &call, const QString &assistantName,
                                const QString &assistantSaid, const QString &question)
{
    ChangeCard card;
    card.kind = call.kind;
    const QString name = shortName(call);
    const QString command = call.command();

    if (call.kind == QLatin1String("edit")) {
        card.heading = name.isEmpty() ? QStringLiteral("Change a file") : QStringLiteral("Change %1").arg(name);
        card.whatChanges = name.isEmpty() ? QStringLiteral("A file on this computer would be changed.")
                                          : QStringLiteral("The file %1 would be changed.").arg(name);
    } else if (call.kind == QLatin1String("delete")) {
        card.heading = name.isEmpty() ? QStringLiteral("Delete a file") : QStringLiteral("Delete %1").arg(name);
        card.whatChanges = name.isEmpty() ? QStringLiteral("Something on this computer would be deleted.")
                                          : QStringLiteral("%1 would be deleted.").arg(name);
    } else if (call.kind == QLatin1String("move")) {
        card.heading = name.isEmpty() ? QStringLiteral("Move a file") : QStringLiteral("Move %1").arg(name);
        card.whatChanges = QStringLiteral("A file would be moved or renamed.");
    } else if (call.kind == QLatin1String("execute")) {
        card.heading = QStringLiteral("Run a command on this computer");
        card.whatChanges = QStringLiteral("A command would run. Jack can't yet tell what it would change.");
    } else if (call.kind == QLatin1String("fetch")) {
        card.heading = QStringLiteral("Fetch something from the internet");
        card.whatChanges = QStringLiteral("Something would be fetched from the internet.");
    } else {
        card.heading = QStringLiteral("Make a change");
        card.whatChanges = QStringLiteral("Something on this computer would change. Jack can't yet tell exactly what.");
    }

    card.why = lastSaid(assistantSaid);
    if (card.why.isEmpty())
        card.why = QStringLiteral("%1 asked for this to answer “%2”.").arg(assistantName, question);

    card.undo = QStringLiteral("Jack can't undo changes yet, so it was not done.");
    card.password = needsPassword(command)
        ? QStringLiteral("Needed. Jack would use the system's own prompt and never see it.")
        : QStringLiteral("Jack can't check this yet.");

    QStringList details;
    details.append(QStringLiteral("Asked by: %1").arg(assistantName));
    if (!call.title.isEmpty())
        details.append(QStringLiteral("Request: %1").arg(call.title));
    if (!call.kind.isEmpty())
        details.append(QStringLiteral("Kind: %1").arg(call.kind));
    if (!command.isEmpty())
        details.append(QStringLiteral("Command: %1").arg(command));
    QStringList files = call.paths;
    for (const QString &path : call.diffPaths) {
        if (!files.contains(path))
            files.append(path);
    }
    for (const QString &file : files)
        details.append(QStringLiteral("File: %1").arg(file));
    details.append(QStringLiteral("Refused: yes, nothing was changed"));
    card.details = details.join(QLatin1Char('\n'));
    return card;
}

QVariantMap ChangeCard::toMap() const
{
    return {{QStringLiteral("kind"), kind},
            {QStringLiteral("heading"), heading},
            {QStringLiteral("whatChanges"), whatChanges},
            {QStringLiteral("why"), why},
            {QStringLiteral("undo"), undo},
            {QStringLiteral("password"), password},
            {QStringLiteral("details"), details}};
}

} // namespace splitrock
