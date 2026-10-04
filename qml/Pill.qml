// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A rounded button: filled for the main action, outlined otherwise.
Rectangle {
    id: pill
    property alias text: label.text
    property bool filled: false
    property int fontSize: 14
    property int padding: 18
    property string iconPath
    signal clicked()

    implicitHeight: 44
    implicitWidth: row.implicitWidth + padding * 2
    radius: height / 2
    color: filled ? theme.primary : theme.chip
    border.width: filled ? 0 : 1
    border.color: theme.edge
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
            color: pill.filled ? theme.primaryInk : theme.text
            font.pixelSize: pill.fontSize
            font.weight: pill.filled ? Font.ExtraBold : Font.DemiBold
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: height / 2
        color: "transparent"
        border.width: 2
        border.color: theme.palette.highlight
        visible: pill.activeFocus
    }

    MouseArea {
        anchors.fill: parent
        enabled: pill.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: pill.clicked()
    }
}
