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

// The mark's drawing, read from the one SVG file every surface uses: each
// path's data, joined, ready for a canvas that draws SVG paths.
QString markPath(const QString &svg);

} // namespace splitrock
