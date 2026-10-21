// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

namespace splitrock::ask {

// The fence llama-server and Codex run in (docs/DECISIONS.md, Ask on this
// computer, and Codex runs in the same fence): the computer, not the
// program's good behaviour, keeps each to what it is given.
//
// It sees the programs and the system's own settings read-only, and nothing
// else of the computer: no other account's home, no drive mounted beside the
// system, nothing of the person's own but the folders it is given. It reaches
// no other program: not the desktop's message buses, not the display, not the
// person's processes. It writes only in the folders it is given; anything
// else it writes is gone when it stops. It keeps the network.
struct FenceRoom {
    // Folders or files inside the home folder it must still read.
    QStringList readable;
    // Folders it may write, made where missing.
    QStringList writable;
    // Devices it may reach, such as the graphics chip.
    QStringList devices;
};

// The program the fence is made with, or empty where this computer has none.
QString fenceProgram();

// The fence program's arguments that start program inside the fence.
QStringList fenced(const QString &program, const QStringList &arguments, const FenceRoom &room);

// What of the person's environment goes inside: where things are, the
// language, the time zone and the way to the network. Nothing else, so no
// key, token or password set for another program reaches it.
QProcessEnvironment fencedEnvironment(const QProcessEnvironment &base);

} // namespace splitrock::ask
