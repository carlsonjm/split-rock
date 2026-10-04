// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

namespace splitrock {

// Passwords, card numbers, bank details and codes sent by text or email are
// never written down (docs/MEMORY.md). Every write to the notebook is checked:
// empty when text may be written, otherwise why not, in plain words.
QString neverWrittenReason(const QString &text);

} // namespace splitrock
