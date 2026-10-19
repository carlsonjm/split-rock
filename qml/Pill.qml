// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A rounded button: filled for the main action, a raised control otherwise.
Rectangle {
    id: pill
    property alias text: label.text
    property bool filled: false
    property int fontSize: 14
    property int padding: 16
    property string iconPath
    // A small dot before the words: something Genie noticed, in its colour.
    property color dot: "transparent"
    // A filled pill's colour, and its ink: the accent unless a card has its
    // own.
    property color tone: theme.accent
    property color toneInk: theme.accentInk
    signal clicked()

    implicitHeight: 40
    implicitWidth: row.implicitWidth + padding * 2
    radius: height / 2
    color: filled ? (area.pressed ? Qt.darker(tone, 1.12) : area.containsMouse ? Qt.lighter(tone, 1.06) : tone)
                  : (area.pressed ? theme.line : area.containsMouse ? theme.chip : theme.raised)
    Behavior on color { ColorAnimation { duration: 120 } }
    opacity: enabled ? 1 : 0.38
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (enabled) clicked()
    Keys.onReturnPressed: if (enabled) clicked()
    Keys.onSpacePressed: if (enabled) clicked()

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Rectangle {
            visible: pill.dot.a > 0
            anchors.verticalCenter: parent.verticalCenter
            width: 8; height: 8
            radius: 4
            color: pill.dot
        }
        Icon {
            visible: pill.iconPath !== ""
            anchors.verticalCenter: parent.verticalCenter
            width: 16; height: 16
            path: pill.iconPath
            color: label.color
        }
        Text {
            id: label
            anchors.verticalCenter: parent.verticalCenter
            color: pill.filled ? pill.toneInk : theme.text
            font.pixelSize: pill.fontSize
            font.weight: Font.Medium
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: height / 2
        color: "transparent"
        border.width: 2
        border.color: theme.text
        visible: pill.activeFocus
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: pill.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: pill.clicked()
    }
}
