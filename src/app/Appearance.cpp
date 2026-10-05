// SPDX-License-Identifier: GPL-2.0-or-later
#include "app/Appearance.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

namespace splitrock {

namespace {

// The value of key in [group] of a KDE settings file, or empty.
QString valueIn(const QString &path, const QString &group, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QString current;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            current = line.mid(1, line.size() - 2);
            continue;
        }
        if (current != group)
            continue;
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        QString name = line.left(equals).trimmed();
        // A locked value, key[$i], is the same key.
        name.remove(QRegularExpression(QStringLiteral("\\[\\$[a-z]+\\]$")));
        if (name == key)
            return line.mid(equals + 1).trimmed();
    }
    return {};
}

QString quoted(const QString &word)
{
    QString escaped = word;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

} // namespace

Appearance Appearance::read(const QString &configFolder, const QStringList &dataFolders)
{
    Appearance a;
    const QString globals = QDir(configFolder).filePath(QStringLiteral("kdeglobals"));
    a.lookAndFeel = valueIn(globals, QStringLiteral("KDE"), QStringLiteral("LookAndFeelPackage"));
    a.colorScheme = valueIn(globals, QStringLiteral("General"), QStringLiteral("ColorScheme"));
    QStringList seenLooks, seenSchemes;
    for (const QString &data : dataFolders) {
        const QDir lnf(QDir(data).filePath(QStringLiteral("plasma/look-and-feel")));
        for (const QString &id : lnf.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (seenLooks.contains(id))
                continue;
            seenLooks.append(id);
            QFile metadata(lnf.filePath(id + QStringLiteral("/metadata.json")));
            QString name;
            if (metadata.open(QIODevice::ReadOnly))
                name = QJsonDocument::fromJson(metadata.readAll()).object().value(QStringLiteral("KPlugin")).toObject()
                           .value(QStringLiteral("Name")).toString();
            a.looks.append(name.isEmpty() ? id : QStringLiteral("%1 (%2)").arg(id, name));
        }
        const QDir colors(QDir(data).filePath(QStringLiteral("color-schemes")));
        for (const QString &file : colors.entryList({QStringLiteral("*.colors")}, QDir::Files, QDir::Name)) {
            const QString id = file.chopped(7);
            if (seenSchemes.contains(id))
                continue;
            seenSchemes.append(id);
            const QString name = valueIn(colors.filePath(file), QStringLiteral("General"), QStringLiteral("Name"));
            a.schemes.append(name.isEmpty() || name == id ? id : QStringLiteral("%1 (%2)").arg(id, name));
        }
    }
    return a;
}

Appearance Appearance::current()
{
    return read(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation),
                QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation));
}

QString Appearance::describe() const
{
    QStringList lines;
    lines << QStringLiteral("The desktop's look now: global theme %1, colour scheme %2.")
                 .arg(lookAndFeel.isEmpty() ? QStringLiteral("(the default)") : lookAndFeel,
                      colorScheme.isEmpty() ? QStringLiteral("(the default)") : colorScheme);
    if (!looks.isEmpty())
        lines << QStringLiteral("Installed global themes, for plasma-apply-lookandfeel -a: %1.").arg(looks.join(QStringLiteral(", ")));
    if (!schemes.isEmpty())
        lines << QStringLiteral("Installed colour schemes, for plasma-apply-colorscheme: %1.").arg(schemes.join(QStringLiteral(", ")));
    return lines.join(QLatin1Char('\n'));
}

bool Appearance::touchedBy(const QString &command)
{
    static const QRegularExpression theme(QStringLiteral(
        "\\b(plasma-apply-lookandfeel|lookandfeeltool|plasma-apply-colorscheme)\\b|\\bkdeglobals\\b"));
    return theme.match(command).hasMatch();
}

QString Appearance::restoreCommand() const
{
    QStringList steps;
    // A global theme brings its own colour scheme, so it goes first.
    if (!lookAndFeel.isEmpty())
        steps << QStringLiteral("plasma-apply-lookandfeel -a %1").arg(quoted(lookAndFeel));
    if (!colorScheme.isEmpty()) {
        steps << QStringLiteral("plasma-apply-colorscheme %1 >/dev/null 2>&1").arg(quoted(colorScheme));
        steps << QStringLiteral("[ \"$(kreadconfig6 --file kdeglobals --group General --key ColorScheme)\" = %1 ]")
                     .arg(quoted(colorScheme));
    }
    return steps.join(QStringLiteral("; "));
}

QString Appearance::checkCommand()
{
    return QStringLiteral("echo \"global theme: $(kreadconfig6 --file kdeglobals --group KDE --key LookAndFeelPackage)\"; "
                          "echo \"colour scheme: $(kreadconfig6 --file kdeglobals --group General --key ColorScheme)\"");
}

} // namespace splitrock
