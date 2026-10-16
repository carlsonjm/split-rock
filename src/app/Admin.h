// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace splitrock {

// split-rock-admin, the only program Split Rock runs as the administrator: it
// holds each Helper's job, by name, and takes only checked arguments
// (docs/DECISIONS.md, The administrator runs only Split Rock's own named jobs).
// Where it is installed.
QString adminHelper();

// Whether a program may be handed to the system's password prompt: a file
// only the administrator can change, in folders only the administrator can
// change, so nothing running as the person can put other words in it. why
// says why not.
bool adminTrusted(const QString &program, QString *why = nullptr);

// The program the system's password prompt runs: elevate, then the helper
// and the job with its arguments. Empty, with why, when the helper can't be
// trusted. Without elevate, as in tests, the helper runs as the test, with
// its logs in logs.
QStringList adminJob(const QStringList &elevate, const QStringList &helper, const QString &job,
                     const QStringList &arguments, const QString &logs = {}, QString *why = nullptr);

} // namespace splitrock
