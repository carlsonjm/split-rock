// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

namespace splitrock {

// What the person sees the assistant called. Inside Shuffle it is Genie; on any
// other Plasma desktop it is Split Rock (docs/DECISIONS.md).
QString productName();
// The name for a window's title bar and the desktop's lists: Shuffle Genie
// inside Shuffle, Split Rock elsewhere.
QString productTitle();
void setInsideShuffle(bool inside);
bool insideShuffle();

// Whether Shuffle's own desktop is installed for this person. Split Rock only
// looks; it never depends on Shuffle.
bool shuffleInstalled();

// What the full view shows of the computer it runs on. Both only read; neither
// changes anything. The maker's name for the hardware, as the computer reports
// it, or empty where it reports none.
QString computerName();
// The desktop, as the session names it: KDE Plasma and its version where it is
// Plasma, with Shuffle beside it where Shuffle is installed.
QString systemName();

// The mark's drawing, read from the one SVG file every surface uses: each
// path's data, joined, ready for a canvas that draws SVG paths.
QString markPath(const QString &svg);

} // namespace splitrock
