// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

namespace splitrock::core {

// Windows programs, Core's fourth topic (docs/CORE.md § Windows programs): a
// Windows installer arriving in the Downloads folder, which can't run here.
// Core offers to find the Linux version, by the name in the file's.

// Whether a file's name is a Windows installer's: .exe or .msi.
bool isWindowsInstaller(const QString &fileName);

// The program a Windows installer is for, from its file's name, without the
// words installers add and their versions: "ZoomInstaller.exe" is "Zoom",
// "vlc-3.0.21-win64.exe" is "vlc". Empty when nothing is left.
QString programName(const QString &fileName);

} // namespace splitrock::core
