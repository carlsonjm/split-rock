// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QStringList>

namespace splitrock {

// How the desktop looks now, read outside the assistant's fence: the global
// theme and colour scheme Plasma uses, and those installed. The assistant can't
// read the person's settings from inside its fence, so it is told; and a theme
// change is undone by putting these back, live.
struct Appearance {
    // The global theme's id, as plasma-apply-lookandfeel -a takes it; empty
    // when none is set.
    QString lookAndFeel;
    // The colour scheme's name, as plasma-apply-colorscheme takes it.
    QString colorScheme;
    // "id (Name)" for each installed global theme and colour scheme.
    QStringList looks;
    QStringList schemes;

    // configFolder: where kdeglobals is; dataFolders: where look-and-feel and
    // color-schemes are found, the person's own first.
    static Appearance read(const QString &configFolder, const QStringList &dataFolders);
    static Appearance current();

    // For the assistant, in a few lines.
    QString describe() const;

    // A command that changes the global theme or colour scheme.
    static bool touchedBy(const QString &command);
    // Puts this look back, live, and fails unless the colour scheme is this
    // one afterwards. Empty when there is nothing to put back.
    QString restoreCommand() const;
    // Reads the global theme and colour scheme back.
    static QString checkCommand();
};

} // namespace splitrock
