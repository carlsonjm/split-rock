// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// What every upkeep card sits in. The window is the card, as Cards draws every
// window, so the frame adds no box of its own: only the Visual sheet's
// comfortable inset and a column for what the card says, its groups a section
// apart.
Item {
    id: frame
    // Which Helper this is, by its colour's name; every Helper draws in the
    // text colour.
    property string hue: "blue"
    readonly property color tone: theme.hue(hue)
    default property alias content: body.data
    property alias spacing: body.spacing
    readonly property real innerWidth: body.width

    implicitHeight: body.implicitHeight + 32

    Column {
        id: body
        x: 16; y: 16
        width: parent.width - 32
        spacing: 20
    }
}
