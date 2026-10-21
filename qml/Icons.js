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
var expand = "M15 3h6v6 M9 21H3v-6 M21 3l-7 7 M3 21l7-7"
var close = "M18 6 6 18 M6 6l12 12"
// The notebook, a panel at the window's right.
var panel = "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z M15 3v18"
// The Update card: turning arrows, a tick, a warning, and a link out.
var refresh = "M21 12a9 9 0 0 1-15.36 6.36L3 16 M3 12a9 9 0 0 1 15.36-6.36L21 8 M21 3v5h-5 M3 21v-5h5"
var check = "M20 6L9 17L4 12"
var alert = "M12 9v4 M12 17h.01 M10.3 3.9L1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"
var shield = "M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"
var lock = "M5 11h14v10H5z M8 11V7a4 4 0 0 1 8 0v4"
var power = "M12 2v10 M18.4 6.6a9 9 0 1 1-12.8 0"
var external = "M15 3h6v6 M10 14L21 3 M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6"
// The other upkeep cards: a drive, a download, going back, a pulse, a
// package and a magnifier.
var drive = "M22 12H2 M5.45 5.11L2 12v6a2 2 0 0 0 2 2h16a2 2 0 0 0 2-2v-6l-3.45-6.89A2 2 0 0 0 16.76 4H7.24a2 2 0 0 0-1.79 1.11z M6 16h.01 M10 16h.01"
var download = "M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4 M7 10l5 5 5-5 M12 15V3"
var history = "M3 12a9 9 0 1 0 3-6.7L3 8 M3 3v5h5 M12 7v5l4 2"
var pulse = "M22 12h-4l-3 9L9 3l-3 9H2"
var apps = "M4 3h5a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z M15 3h5a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1h-5a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1z M4 14h5a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1v-5a1 1 0 0 1 1-1z M15 14h5a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1h-5a1 1 0 0 1-1-1v-5a1 1 0 0 1 1-1z"
var box = "M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z M3.27 6.96L12 12.01l8.73-5.05 M12 22.08V12"
var search = "M11 4a7 7 0 1 0 0 14a7 7 0 1 0 0-14 M21 21l-4.35-4.35"
// A conversation: a speech bubble.
var chat = "M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z"
// Undo: an arrow turning back.
var undo = "M9 14L4 9l5-5 M4 9h10.5a5.5 5.5 0 0 1 0 11H11"
// A setting: a sliders mark.
var sliders = "M4 21v-7 M4 10V3 M12 21v-9 M12 8V3 M20 21v-5 M20 12V3 M1 14h6 M9 8h6 M17 16h6"
var back = "M15 18l-6-6 6-6"
// Tidy up Downloads: down into a tray.
var tidy = "M12 3v12 M7 10l5 5 5-5 M5 21h14"
var folder = "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"

// Each upkeep card's icon.
function forChore(name) {
    return ({ update: refresh, space: drive, install: download, restore: history, health: pulse, community: box, changes: undo, tidy: tidy })[name] || refresh
}
