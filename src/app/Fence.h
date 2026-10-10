// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "acp/Connection.h"

#include <QStringList>

namespace splitrock {

// The fence the assistant's program runs in (docs/DECISIONS.md, The assistant
// is fenced in): the computer, not the assistant's good behaviour, keeps it to
// reading and to writing the notebook.
//
// It sees the system read-only, and the desktop's own settings read-only. The
// person's home folder is hidden, except the notebook and the assistant's own
// folder, which it may write. It reaches no other program: not the desktop's
// message buses, not the display, not the person's processes. What it writes
// anywhere else is lost when it stops. It keeps the network, to reach its
// provider.
struct FenceRoom {
    // Where the assistant may write.
    QStringList writable;
    // Paths inside the home folder it must still read, such as its own
    // program when that lives in a checkout there.
    QStringList readable;
    // A file inside a writable folder where the assistant's requests to open a
    // web page are written, one address a line, by stand-ins for the desktop's
    // openers. The fence reaches no browser itself; Split Rock decides which
    // pages to open. Empty: no page is ever handed out.
    QString openRequests;
    // A file inside a writable folder where the assistant's proposed changes
    // are written, one a line, by the split-rock-change stand-in. Empty: the
    // stand-in is not offered.
    QString changeRequests;
    // A file inside a writable folder where the split-rock-skill stand-in asks
    // for a settings skill, one "token id" a line. Split Rock answers in
    // skill-<token>.out beside it. Empty: the stand-in is not offered.
    QString skillRequests;
    // Devices it may reach, such as the graphics chip for a model that runs
    // on this computer. None otherwise.
    QStringList devices;
};

// The program the fence is made with, or empty where this computer has none.
QString fenceProgram();

// The launch, inside the fence.
acp::LaunchSpec fenced(const acp::LaunchSpec &launch, const FenceRoom &room);

} // namespace splitrock
