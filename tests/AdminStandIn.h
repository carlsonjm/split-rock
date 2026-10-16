// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QStringList>

// split-rock-admin as the source has it, run as the test.
inline QStringList adminInSource()
{
    return {QStringLiteral(PROJECT_DIR "/src/admin/split-rock-admin")};
}

// A stand-in for split-rock-admin: a script given the job's name and its
// arguments.
inline QStringList adminStandIn(const QString &script)
{
    return {QStringLiteral("/bin/sh"), QStringLiteral("-c"), script, QStringLiteral("split-rock-admin")};
}
