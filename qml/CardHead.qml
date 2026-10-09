// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A card's heading: its icon in a tile of its colour, what it is and where it
// has got to, and a line under it.
Row {
    id: head
    property string icon
    property color tone: theme.accent
    property string heading
    property string sub
    // The icon turns while the card looks or works.
    property bool spinning: false
    property alias headingObjectName: headingText.objectName

    width: parent ? parent.width : 0
    spacing: 14

    Rectangle {
        width: 48; height: 48; radius: 15
        gradient: Gradient {
            GradientStop { position: 0; color: theme.soft(head.tone, theme.dark ? 0.32 : 0.22) }
            GradientStop { position: 1; color: theme.soft(head.tone, theme.dark ? 0.16 : 0.10) }
        }
        Icon {
            id: headIcon
            anchors.centerIn: parent
            width: 24; height: 24
            stroke: 2
            path: head.icon
            color: head.tone
            RotationAnimation on rotation {
                running: head.spinning
                loops: Animation.Infinite
                from: 0; to: 360
                duration: 2400
                // Upright again once it stops, for the tick or the warning
                // that follows.
                onRunningChanged: if (!running) headIcon.rotation = 0
            }
        }
    }

    Column {
        anchors.verticalCenter: parent.verticalCenter
        width: head.width - 62
        spacing: 3
        Text {
            id: headingText
            width: parent.width
            wrapMode: Text.WordWrap
            text: head.heading
            color: theme.text
            font.pixelSize: 22
            font.weight: Font.Bold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: text !== ""
            text: head.sub
            color: theme.quiet
            font.pixelSize: 15
            lineHeight: 1.3
        }
    }
}
