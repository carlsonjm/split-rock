// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// What every upkeep card sits in: the raised card, washed at its top with the
// card's own colour, and a column for what it says.
Rectangle {
    id: frame
    // One of theme.hues' names.
    property string hue: "blue"
    readonly property color tone: theme.hue(hue)
    default property alias content: body.data
    property alias spacing: body.spacing
    readonly property real innerWidth: body.width

    implicitHeight: body.implicitHeight + 48
    radius: theme.cardRadius
    color: theme.raised
    border.width: 1
    border.color: theme.hairline

    Rectangle {
        x: 1; y: 1
        width: parent.width - 2
        height: Math.min(parent.height - 2, 180)
        radius: theme.cardRadius - 1
        gradient: Gradient {
            GradientStop { position: 0; color: theme.soft(frame.tone, theme.dark ? 0.18 : 0.12) }
            GradientStop { position: 1; color: theme.soft(frame.tone, 0) }
        }
    }

    Column {
        id: body
        x: 24; y: 24
        width: parent.width - 48
        spacing: 18
    }
}
