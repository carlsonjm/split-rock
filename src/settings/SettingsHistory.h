// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace splitrock {

// One setting that changed in one of the desktop's settings files: where,
// when, and its value before and after.
struct SettingsChange {
    QDateTime when;
    // The file, as the settings folder names it, such as "kwinrc".
    QString file;
    // KDE's group, nested groups joined by "][" as the file writes them, such
    // as "Containments][12][Applets][40". For kwinoutputconfig.json, the
    // output's connector, as "eDP-1".
    QString group;
    QString key;
    QString before;
    QString after;
    // The key was there before, and is there after.
    bool wasSet = true;
    bool isSet = true;

    QJsonObject toJson() const;
    static SettingsChange fromJson(const QJsonObject &object);
    bool operator==(const SettingsChange &other) const = default;
};

// The copies Core keeps of the desktop's settings files, and what changed
// in them (docs/CORE.md, Settings copies).
//
// Each look compares every file What changed reads with Core's own copy of
// it. A key whose value differs is one change, kept with the time; then the
// copy is brought up to date. A file the desktop wrote again with the same
// values changes nothing, and keys the desktop keeps for itself, such as
// window sizes, are left out. The first look only takes the copies.
//
// It reads and writes only its own folder; the settings are read, never
// changed.
class SettingsHistory
{
public:
    // folder: where the copies and the list of changes are kept. configHome:
    // the person's settings folder.
    SettingsHistory(const QString &folder, const QString &configHome);

    static QString defaultFolder();

    // The files compared, by their names in the settings folder.
    static QStringList watched();
    QString configHome() const { return m_configHome; }

    // Compares every file with its copy now; what changed is kept and
    // returned. Changes are kept for 30 days.
    QList<SettingsChange> look(const QDateTime &now);
    // What changed since then, oldest first.
    QList<SettingsChange> changes(const QDateTime &since = {}) const;
    // When the copies were first taken; invalid before.
    QDateTime keptSince() const;

    // The path of one of the files compared.
    QString pathOf(const QString &file) const;

    // A settings file read as KDE writes it: each group's keys and values.
    static QMap<QString, QMap<QString, QString>> readIni(const QByteArray &text);
    // kwinoutputconfig.json read as each output's settings, by its connector.
    static QMap<QString, QMap<QString, QString>> readOutputs(const QByteArray &json);
    // What differs between two versions of one file, the desktop's own
    // keeping left out.
    static QList<SettingsChange> compare(const QString &file, const QByteArray &before, const QByteArray &after,
                                         const QDateTime &when);
    // A key the desktop writes for itself, which no person sets.
    static bool kept(const QString &file, const QString &group, const QString &key);

private:
    QString copyOf(const QString &file) const;
    void append(const QList<SettingsChange> &changes, const QDateTime &now) const;

    QString m_folder;
    QString m_configHome;
};

} // namespace splitrock
