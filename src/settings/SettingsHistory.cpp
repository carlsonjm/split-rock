// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings/SettingsHistory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace splitrock {

namespace {

constexpr qint64 Largest = 2 * 1024 * 1024;
constexpr int KeptDays = 30;
const QString Outputs = QStringLiteral("kwinoutputconfig.json");
const QString Applets = QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc");

QByteArray readFile(const QString &path, bool *ok = nullptr)
{
    QFile file(path);
    const bool opened = QFileInfo(path).size() <= Largest && file.open(QIODevice::ReadOnly);
    if (ok)
        *ok = opened;
    return opened ? file.readAll() : QByteArray();
}

// A JSON value as one line of text.
QString said(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isBool())
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (value.isDouble())
        return QString::number(value.toDouble(), 'g', 12);
    if (value.isNull() || value.isUndefined())
        return {};
    const QJsonDocument document = value.isArray() ? QJsonDocument(value.toArray()) : QJsonDocument(value.toObject());
    return QString::fromUtf8(document.toJson(QJsonDocument::Compact));
}

} // namespace

QJsonObject SettingsChange::toJson() const
{
    QJsonObject object{{QStringLiteral("when"), when.toUTC().toString(Qt::ISODate)},
                       {QStringLiteral("file"), file},
                       {QStringLiteral("group"), group},
                       {QStringLiteral("key"), key}};
    if (wasSet)
        object.insert(QStringLiteral("before"), before);
    if (isSet)
        object.insert(QStringLiteral("after"), after);
    return object;
}

SettingsChange SettingsChange::fromJson(const QJsonObject &object)
{
    SettingsChange change;
    change.when = QDateTime::fromString(object.value(QStringLiteral("when")).toString(), Qt::ISODate);
    change.file = object.value(QStringLiteral("file")).toString();
    change.group = object.value(QStringLiteral("group")).toString();
    change.key = object.value(QStringLiteral("key")).toString();
    change.wasSet = object.contains(QStringLiteral("before"));
    change.isSet = object.contains(QStringLiteral("after"));
    change.before = object.value(QStringLiteral("before")).toString();
    change.after = object.value(QStringLiteral("after")).toString();
    return change;
}

SettingsHistory::SettingsHistory(const QString &folder, const QString &configHome)
    : m_folder(folder)
    , m_configHome(configHome)
{
}

QString SettingsHistory::defaultFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/split-rock/core/settings");
}

QStringList SettingsHistory::watched()
{
    // The files What changed names (app/RecentChanges.cpp, settingsName).
    return {QStringLiteral("kdeglobals"),
            QStringLiteral("kwinrc"),
            QStringLiteral("kwinrulesrc"),
            Applets,
            QStringLiteral("plasmarc"),
            QStringLiteral("kcminputrc"),
            QStringLiteral("touchpadxlibinputrc"),
            QStringLiteral("kxkbrc"),
            QStringLiteral("kglobalshortcutsrc"),
            QStringLiteral("powerdevilrc"),
            QStringLiteral("powermanagementprofilesrc"),
            QStringLiteral("kscreenlockerrc"),
            QStringLiteral("bluedevilglobalrc"),
            QStringLiteral("plasmanotifyrc"),
            QStringLiteral("mimeapps.list"),
            QStringLiteral("kcmfonts"),
            QStringLiteral("baloofilerc"),
            QStringLiteral("kaccessrc"),
            QStringLiteral("ktimezonedrc"),
            QStringLiteral("plasma-localerc"),
            Outputs};
}

QString SettingsHistory::pathOf(const QString &file) const
{
    return QDir(m_configHome).filePath(file);
}

QString SettingsHistory::copyOf(const QString &file) const
{
    return QDir(m_folder).filePath(QStringLiteral("copies/") + file);
}

QMap<QString, QMap<QString, QString>> SettingsHistory::readIni(const QByteArray &text)
{
    QMap<QString, QMap<QString, QString>> groups;
    QString group;
    for (const QByteArray &raw : text.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            group = line.mid(1, line.size() - 2);
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        groups[group].insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    return groups;
}

QMap<QString, QMap<QString, QString>> SettingsHistory::readOutputs(const QByteArray &json)
{
    // KWin keeps every output it has known, each with its mode, scale and
    // turn, and the setups they were arranged in.
    QMap<QString, QMap<QString, QString>> outputs;
    const QJsonArray sections = QJsonDocument::fromJson(json).array();
    QStringList connectors;
    for (const QJsonValue &section : sections) {
        if (section[QStringLiteral("name")].toString() != QLatin1String("outputs"))
            continue;
        for (const QJsonValue &output : section[QStringLiteral("data")].toArray()) {
            QString connector = output[QStringLiteral("connectorName")].toString();
            if (connector.isEmpty())
                connector = QStringLiteral("output %1").arg(connectors.size() + 1);
            // The same connector with another screen plugged into it.
            if (connectors.contains(connector))
                connector += QStringLiteral(" (") + output[QStringLiteral("edidIdentifier")].toString() + QLatin1Char(')');
            connectors.append(connector);
            QMap<QString, QString> &keys = outputs[connector];
            const QJsonObject object = output.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                if (it.key() == QLatin1String("mode")) {
                    const QJsonObject mode = it.value().toObject();
                    keys.insert(QStringLiteral("mode"), QStringLiteral("%1x%2@%3")
                                                            .arg(mode[QStringLiteral("width")].toInt())
                                                            .arg(mode[QStringLiteral("height")].toInt())
                                                            .arg(mode[QStringLiteral("refreshRate")].toInt()));
                } else if (!it.value().isObject() && !it.value().isArray()) {
                    keys.insert(it.key(), said(it.value()));
                }
            }
        }
    }
    for (const QJsonValue &section : sections) {
        if (section[QStringLiteral("name")].toString() != QLatin1String("setups"))
            continue;
        for (const QJsonValue &setup : section[QStringLiteral("data")].toArray()) {
            // A setup is known by the outputs in it.
            QStringList in;
            const QJsonArray members = setup[QStringLiteral("outputs")].toArray();
            for (const QJsonValue &member : members)
                in.append(connectors.value(member[QStringLiteral("outputIndex")].toInt()));
            in.sort();
            const QString with = in.join(QLatin1Char('+')) + (setup[QStringLiteral("lidClosed")].toBool() ? QStringLiteral(" lid closed") : QString());
            for (const QJsonValue &member : members) {
                const QString connector = connectors.value(member[QStringLiteral("outputIndex")].toInt());
                if (connector.isEmpty())
                    continue;
                const QJsonObject position = member[QStringLiteral("position")].toObject();
                outputs[connector].insert(QStringLiteral("enabled in ") + with, said(member[QStringLiteral("enabled")]));
                outputs[connector].insert(QStringLiteral("position in ") + with,
                                          QStringLiteral("%1,%2").arg(position[QStringLiteral("x")].toInt()).arg(position[QStringLiteral("y")].toInt()));
            }
        }
    }
    return outputs;
}

bool SettingsHistory::kept(const QString &file, const QString &group, const QString &key)
{
    if (file == Applets) {
        // Plasma writes its panels' every size and place; what a person
        // does is add or remove a widget or a panel, or change the wallpaper.
        static const QRegularExpression widget(QStringLiteral(R"(^Containments\]\[\d+(\]\[Applets\]\[\d+)?$)"));
        static const QRegularExpression wallpaper(QStringLiteral(R"(^Containments\]\[\d+\]\[Wallpaper\]\[[^\]]+\]\[General$)"));
        if (widget.match(group).hasMatch())
            return key != QLatin1String("plugin");
        return !(wallpaper.match(group).hasMatch() && key == QLatin1String("Image"));
    }
    if (file == Outputs) {
        static const QStringList outputKept{QStringLiteral("edidHash"), QStringLiteral("edidIdentifier"),
                                            QStringLiteral("connectorName"), QStringLiteral("mstPath"),
                                            QStringLiteral("uuid")};
        return outputKept.contains(key);
    }
    if (file == QLatin1String("kglobalshortcutsrc")) {
        // The desktop writes each component's own name; a person sets keys.
        return key == QLatin1String("_k_friendly_name") || group == QLatin1String("$Version");
    }
    if (group.startsWith(QLatin1Char('$')) || key.startsWith(QLatin1String("$")))
        return true;
    // What programs keep for themselves in the shared files: window sizes and
    // places, splitter positions, recent lists and stamps.
    static const QRegularExpression own(
        QStringLiteral(R"((Geometr|^Width$|^Height$|^Size$|popupWidth|popupHeight|DialogSize|Splitter|State$|Timestamp|LastUsed|Recent|History|lastScreen|Window-Maximized|RestorePositionForNextInstance|^\d+ screens?:|^X$|^Y$|ToolBarsMovable|ViewPropsTimestamp|LastLocation|^ScreenConnectors$))"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression ownGroup(
        QStringLiteral(R"((MainWindow|KFileDialog|FileDialogSize|Recent|History|^Tiling\]|^Desktops$|^Activities|^SubSession|^Xwayland$|^ModifierOnlyShortcuts$|^Script-|^\$Version))"),
        QRegularExpression::CaseInsensitiveOption);
    return own.match(key).hasMatch() || ownGroup.match(group).hasMatch();
}

QList<SettingsChange> SettingsHistory::compare(const QString &file, const QByteArray &before, const QByteArray &after,
                                               const QDateTime &when)
{
    const bool outputs = file == Outputs;
    const auto was = outputs ? readOutputs(before) : readIni(before);
    const auto now = outputs ? readOutputs(after) : readIni(after);
    QList<SettingsChange> changes;
    QStringList groups = was.keys() + now.keys();
    groups.removeDuplicates();
    groups.sort();
    for (const QString &group : std::as_const(groups)) {
        const QMap<QString, QString> a = was.value(group);
        const QMap<QString, QString> b = now.value(group);
        QStringList keys = a.keys() + b.keys();
        keys.removeDuplicates();
        keys.sort();
        for (const QString &key : std::as_const(keys)) {
            const bool wasSet = a.contains(key);
            const bool isSet = b.contains(key);
            if (wasSet == isSet && a.value(key) == b.value(key))
                continue;
            if (kept(file, group, key))
                continue;
            changes.append({when, file, group, key, a.value(key), b.value(key), wasSet, isSet});
        }
    }
    return changes;
}

QList<SettingsChange> SettingsHistory::look(const QDateTime &now)
{
    const bool first = !QFileInfo::exists(QDir(m_folder).filePath(QStringLiteral("since")));
    QDir().mkpath(QDir(m_folder).filePath(QStringLiteral("copies")));
    QList<SettingsChange> found;
    for (const QString &file : watched()) {
        bool readable = false;
        const QString path = pathOf(file);
        const bool exists = QFileInfo::exists(path);
        const QByteArray current = exists ? readFile(path, &readable) : QByteArray();
        if (exists && !readable)
            continue;
        const QString copy = copyOf(file);
        bool hadCopy = false;
        const QByteArray kept = readFile(copy, &hadCopy);
        if (hadCopy && kept == current)
            continue;
        // A file made since is compared with nothing, so what it was made
        // with is a change too.
        if (!first && (hadCopy || exists))
            found += compare(file, kept, current, now);
        if (!exists) {
            QFile::remove(copy);
            continue;
        }
        QSaveFile out(copy);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(current);
            out.commit();
        }
    }
    if (first) {
        QSaveFile since(QDir(m_folder).filePath(QStringLiteral("since")));
        if (since.open(QIODevice::WriteOnly)) {
            since.write(now.toUTC().toString(Qt::ISODate).toUtf8() + '\n');
            since.commit();
        }
    }
    if (!found.isEmpty())
        append(found, now);
    return found;
}

void SettingsHistory::append(const QList<SettingsChange> &changes, const QDateTime &now) const
{
    // Written whole, the last 30 days and these.
    QList<SettingsChange> all = this->changes(now.addDays(-KeptDays));
    all += changes;
    QByteArray text;
    for (const SettingsChange &change : std::as_const(all))
        text += QJsonDocument(change.toJson()).toJson(QJsonDocument::Compact) + '\n';
    QSaveFile out(QDir(m_folder).filePath(QStringLiteral("changes.jsonl")));
    if (out.open(QIODevice::WriteOnly)) {
        out.write(text);
        out.commit();
    }
}

QList<SettingsChange> SettingsHistory::changes(const QDateTime &since) const
{
    QList<SettingsChange> changes;
    const QByteArray text = readFile(QDir(m_folder).filePath(QStringLiteral("changes.jsonl")));
    for (const QByteArray &line : text.split('\n')) {
        if (line.trimmed().isEmpty())
            continue;
        const SettingsChange change = SettingsChange::fromJson(QJsonDocument::fromJson(line).object());
        if (change.when.isValid() && (!since.isValid() || change.when >= since))
            changes.append(change);
    }
    return changes;
}

QDateTime SettingsHistory::keptSince() const
{
    return QDateTime::fromString(QString::fromUtf8(readFile(QDir(m_folder).filePath(QStringLiteral("since")))).trimmed(),
                                 Qt::ISODate);
}

} // namespace splitrock
