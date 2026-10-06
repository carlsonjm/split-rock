// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// Colours follow the desktop's own theme; the mock-up fixes layout, sizes and
// touch targets. Surfaces are tints of the window colour toward its text, so
// light and dark themes keep the mock-up's contrast steps.
QtObject {
    property SystemPalette palette: SystemPalette { colorGroup: SystemPalette.Active }

    function tint(amount) {
        return Qt.tint(palette.window, Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, amount))
    }

    readonly property color sheet: palette.window
    readonly property color text: palette.windowText
    readonly property color panel: tint(0.05)
    readonly property color chip: tint(0.10)
    readonly property color line: tint(0.12)
    readonly property color edge: tint(0.30)
    readonly property color quiet: tint(0.60)
    readonly property color primary: palette.windowText
    readonly property color primaryInk: palette.window
    // The assistant's mark, in the app icon's blue tile.
    readonly property color mark: "#8BC4F8"
    readonly property color markInk: "#1A1A1A"
    readonly property string mono: "monospace"
}
