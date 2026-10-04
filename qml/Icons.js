// SPDX-License-Identifier: GPL-2.0-or-later
.pragma library

// Line icons from the mock-up, as SVG path data on a 24-unit grid.
var arrow = "M5 12h14 M12 5l7 7-7 7"
var stop = "M7 7h10v10H7z"
var chevron = "M6 9l6 6 6-6"
var pencil = "M12 20h9 M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z"
var trash = "M3 6h18 M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6 M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"
var bolt = "M13 2L3 14h9l-1 8 10-12h-9l1-8z"
var globe = "M12 2a10 10 0 1 0 0 20a10 10 0 1 0 0-20 M12 2a14.5 14.5 0 0 0 0 20 14.5 14.5 0 0 0 0-20 M2 12h20"

function forKind(kind) {
    if (kind === "edit" || kind === "move")
        return pencil
    if (kind === "delete")
        return trash
    if (kind === "fetch")
        return globe
    return bolt
}
