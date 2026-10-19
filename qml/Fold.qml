// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// "Show details": closed until tapped. Holds exact commands and messages that
// the person never needs to read.
Column {
    id: fold
    property string title: "Show details"
    property string body
    property bool open: false
    // The tallest the opened box grows before it scrolls.
    property real longest: 320
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
                font.weight: Font.Medium
            }
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: fold.open = !fold.open
        }
    }

    // A long one, such as everything an update printed, scrolls inside its
    // own box, newest at the bottom, so the card's heading and buttons stay
    // in reach.
    Rectangle {
        visible: fold.open
        width: fold.width
        height: Math.min(detail.implicitHeight, fold.longest) + 24
        radius: theme.cardRadius
        color: theme.sheet
        border.width: 1
        border.color: theme.line
        Flickable {
            id: scroller
            x: 14; y: 12
            width: parent.width - 28
            height: parent.height - 24
            contentWidth: width
            contentHeight: detail.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)
            TextEdit {
                id: detail
                width: scroller.width
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

    // The way back to the card from a long one.
    Item {
        visible: fold.open && detail.implicitHeight > fold.longest
        width: hideText.implicitWidth + 24
        height: 32
        Accessible.role: Accessible.Button
        Accessible.name: "Hide"
        Text {
            id: hideText
            anchors.verticalCenter: parent.verticalCenter
            text: "▴ Hide"
            color: theme.quiet
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: fold.open = false
        }
    }
}
