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
    readonly property bool dark: palette.window.hslLightness < 0.5
    // A step up from the sheet, for what floats on it: the composer, cards.
    readonly property color raised: dark ? tint(0.07) : Qt.tint(palette.window, Qt.rgba(1, 1, 1, 0.75))
    readonly property color hairline: tint(dark ? 0.11 : 0.09)
    // A step down inside a card, for its rows.
    readonly property color inset: dark ? tint(0.11) : Qt.tint(palette.window, Qt.rgba(1, 1, 1, 0.45))
    // The conversation's colour: the mark's blue, deepened on a light sheet
    // so text on it stays readable. Primary actions, progress and focus take
    // it; what the person asked runs from it into accentTo.
    readonly property color accent: dark ? "#8BC4F8" : "#1E64C8"
    readonly property color accentTo: dark ? "#B9A3FF" : "#6741D9"
    readonly property color accentInk: dark ? "#0B1726" : "#FFFFFF"
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.14 : 0.10)
    readonly property color warn: dark ? "#F2C46B" : "#9A6200"
    readonly property color bad: dark ? "#F28B82" : "#B3261E"
    readonly property color good: dark ? "#7FD6A4" : "#1E7A45"
    // The assistant's mark, in the app icon's blue tile.
    readonly property color mark: "#8BC4F8"
    readonly property color markInk: "#1A1A1A"
    // Each upkeep card's own colour, deepened on a light sheet the same way:
    // the card's wash, its icon and its main button take it.
    readonly property var hues: ({
        blue: dark ? "#8BC4F8" : "#1E64C8",
        teal: dark ? "#6FD8CB" : "#0B7A70",
        violet: dark ? "#B9A3FF" : "#6741D9",
        amber: dark ? "#F2C46B" : "#9A6200",
        coral: dark ? "#FF9E8A" : "#C2410C",
        pink: dark ? "#F49AC8" : "#B5287A",
        green: dark ? "#7FD6A4" : "#1E7A45",
        orange: dark ? "#FFB46B" : "#B45309"
    })
    function hue(name) {
        return hues[name] || accent
    }
    function soft(color, amount) {
        const c = Qt.color(color)
        return Qt.rgba(c.r, c.g, c.b, amount !== undefined ? amount : (dark ? 0.16 : 0.12))
    }
    // The ink on a button filled with a card's colour.
    readonly property color hueInk: dark ? "#10141A" : "#FFFFFF"
    // Which colour each card is.
    readonly property var choreHue: ({
        update: "blue", space: "teal", install: "violet", restore: "amber", health: "green", community: "orange"
    })
    readonly property string mono: "monospace"
    // Corners: cards, the composer and bubbles.
    readonly property int cardRadius: 22
    readonly property int bubbleRadius: 20
}
