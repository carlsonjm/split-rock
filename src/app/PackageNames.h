// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace splitrock {

// Packages by the names people know them by, for lists a person reads: Free
// up space's old copies and Check for updates' list. A package's launcher
// gives its name where it has one; a few well-known parts with no launcher
// have one here; the rest are the system's own parts (docs/DECISIONS.md).

// The name a package with no launcher is known by, or empty for one of the
// system's own parts.
QString knownPackageName(const QString &package);

// A look's section of "package<TAB>launcher name" lines, as
// launcherNamesScript() prints them, read into a map.
QMap<QString, QString> readLauncherNames(const QStringList &lines);

// Shell, run as the person: reads package names from its input, one a line,
// and prints "package<TAB>name" for each with a launcher installed.
QString launcherNamesScript();

// The name a package is known by: its launcher's, then knownPackageName's;
// empty for one of the system's own parts.
QString packageName(const QString &package, const QMap<QString, QString> &launchers);

} // namespace splitrock
