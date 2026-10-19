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
    readonly property color chip: tint(0.125)
    readonly property color line: tint(0.12)
    readonly property color edge: tint(0.30)
    readonly property color quiet: tint(0.60)
    readonly property color primary: palette.windowText
    readonly property color primaryInk: palette.window
    readonly property bool dark: palette.window.hslLightness < 0.5
    // A step up from the sheet, for what floats on it: the composer, cards.
    readonly property color raised: dark ? tint(0.07) : Qt.tint(palette.window, Qt.rgba(1, 1, 1, 0.75))
    readonly property color hairline: tint(dark ? 0.11 : 0.09)
    // The sheet's subtle fill, inside a card, for its rows.
    readonly property color inset: dark ? tint(0.075) : Qt.tint(palette.window, Qt.rgba(1, 1, 1, 0.45))
    // The Visual sheet's accent is the text colour itself: suite chrome stays
    // monochrome, and only state (warn, bad, good) takes a colour. Primary
    // actions, progress and focus take it.
    readonly property color accent: palette.windowText
    readonly property color accentInk: dark ? "#102729" : palette.window
    readonly property color warn: dark ? "#F2A65A" : "#9A6200"
    readonly property color bad: dark ? "#FFB5A8" : "#B3261E"
    readonly property color good: dark ? "#00D3B8" : "#0B7A70"
    // Every Helper draws in the text colour, as the Visual sheet asks of the
    // suite's own chrome; a Helper's colour name is kept only to say which it
    // is.
    function hue(name) {
        return accent
    }
    function soft(color, amount) {
        const c = Qt.color(color)
        return Qt.rgba(c.r, c.g, c.b, amount !== undefined ? amount : (dark ? 0.16 : 0.12))
    }
    // The ink on a button filled with a card's colour.
    readonly property color hueInk: accentInk
    readonly property string mono: "monospace"
    // Corners: the Visual sheet's paper tier, for the card and everything laid
    // in it.
    readonly property int cardRadius: 10
}
