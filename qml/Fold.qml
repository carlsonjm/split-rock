// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// "Show details": closed until tapped. Holds exact commands and messages that
// the person never needs to read.
Column {
    id: fold
    property string title: "Show details"
    property string body
    property bool open: false
    spacing: 8
    visible: body !== ""

    Item {
        width: summary.implicitWidth + 24
        height: 32
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: fold.title
        Keys.onReturnPressed: fold.open = !fold.open
        Keys.onSpacePressed: fold.open = !fold.open
        Row {
            id: summary
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Text {
                text: fold.open ? "▾" : "▸"
                color: theme.quiet
                font.pixelSize: 13
            }
            Text {
                text: fold.title
                color: theme.quiet
                font.pixelSize: 13
                font.weight: Font.Bold
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: fold.open = !fold.open
        }
    }

    Rectangle {
        visible: fold.open
        width: fold.width
        height: detail.implicitHeight + 24
        radius: 10
        color: theme.sheet
        border.width: 1
        border.color: theme.line
        TextEdit {
            id: detail
            x: 14; y: 12
            width: parent.width - 28
            text: fold.body
            readOnly: true
            selectByMouse: true
            wrapMode: Text.WrapAnywhere
            color: theme.text
            font.family: theme.mono
            font.pixelSize: 13
        }
    }
}
