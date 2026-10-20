// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace splitrock::ask {

// The fence llama-server runs in (docs/DECISIONS.md, Ask on this computer): the
// computer, not the engine's good behaviour, keeps it to reading the model.
//
// It sees the system read-only. The person's home folder is hidden, except the
// folders it is given to read, such as the model's. It reaches no other
// program: not the desktop's message buses, not the display, not the person's
// processes, and it writes nowhere that outlasts it. It keeps the network on
// this computer, where Ask reaches it on a local port with a key.
struct FenceRoom {
    // Folders or files inside the home folder it must still read.
    QStringList readable;
    // Devices it may reach, such as the graphics chip.
    QStringList devices;
};

// The program the fence is made with, or empty where this computer has none.
QString fenceProgram();

// The fence program's arguments that start program inside the fence.
QStringList fenced(const QString &program, const QStringList &arguments, const FenceRoom &room);

} // namespace splitrock::ask
