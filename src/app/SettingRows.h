// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "app/Change.h"
#include "settings/SettingsHistory.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <functional>

namespace splitrock {

// The settings that changed, said for What changed: a row each for what a
// person changed in one place at one time, in words, with its way back.
//
// The way back is one of three. "putback" writes the old values again and
// tells the running desktop, as a change from a card with its own Undo, where
// that is known to take effect live: screens' scale, mode and turn, keyboard
// shortcuts, windows and effects, window rules, keyboard layouts, power,
// screen lock, notifications, default apps and the wallpaper. "settings" opens
// KDE's own page for it, where the old value is shown on the row. "help"
// copies the change, exactly, to ask someone, as for a panel's widgets.
struct SettingRow {
    QString id;
    QDateTime when;
    // What it is, as "Built-in screen: scale 175% to 150%", and under it
    // where and when.
    QString title;
    QString detail;
    // The settings area, as What changed names a file: "Displays".
    QString area;
    QString way;
    // KDE's settings page for it, for "settings" and for Show me how.
    QString page;
    // For "putback": the change that writes the old values, with its undo.
    ChangeProposal putBack;
    // Every change in the row, exactly, as "kwinrc [Plugins] blurEnabled: true
    // to false", for Show details and the help text.
    QStringList exact;
    // What it is in words, for the help text.
    QString question;
    // The values are as they were before already.
    bool alreadyBack = false;
    QList<SettingsChange> changes;
};

class SettingRows
{
public:
    // Reads the value a setting has now, for whether a row is put back
    // already; empty with set false when it isn't set.
    using Current = std::function<QString(const SettingsChange &change, bool *set)>;

    // The changes as rows, newest first: changes in one place within two
    // minutes of each other make one row. program: Split Rock itself, which
    // sets a keyboard shortcut back. Changes nobody made, such as a shortcut's
    // name written again, make no row.
    static QList<SettingRow> rows(const QList<SettingsChange> &changes, const QString &program,
                                  const Current &current = {});

    // One change in words: the title, what it was and is, and whether it
    // reads as something a person did. Exposed for tests.
    struct Words {
        QString title;
        QString wasNow;
        bool meaningful = true;
    };
    static Words words(const SettingsChange &change);
    // The settings area of a file, as What changed says it.
    static QString area(const QString &file);
    // The command that sets one change to its value before, or after; empty
    // when none is known to work live.
    static QString command(const SettingsChange &change, bool before, const QString &program);
    // What tells the running desktop to read a file again after it is
    // written; empty when writing it is enough.
    static QString reload(const QString &file);
    // KDE's settings page for a change.
    static QString page(const SettingsChange &change);
    // A shell word, quoted.
    static QString quoted(const QString &word);
    // An app's name from its desktop file id, as "Firefox".
    static QString appName(const QString &desktopId);
    // "9:14 am".
    static QString timeWords(const QDateTime &when);
};

} // namespace splitrock
