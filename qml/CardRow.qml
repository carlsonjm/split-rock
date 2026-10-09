// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// A row in a card's list: something to pick, open or read. At its start a
// box to tick, an icon in a tile of the row's colour, or a dot for how it
// stands; at its end a size or a count, and a chevron when it opens.
Rectangle {
    id: row
    property string title
    property string detail
    property string trailing
    property color tone: theme.accent
    property string icon
    // A box to tick, instead of the icon.
    property bool checkable: false
    property bool checked: false
    // Opens something: a chevron at its end.
    property bool opens: false
    // good, warn or bad: a dot instead of the icon.
    property string standing
    // What it holds, folded under it.
    property string list
    property string listTitle: "Show which"
    signal clicked()

    readonly property bool tappable: checkable || opens
    readonly property color dot: standing === "bad" ? theme.bad : standing === "warn" ? theme.warn : theme.good
    width: parent ? parent.width : 0
    height: Math.max(64, rowColumn.implicitHeight + 24)
    radius: 16
    color: area.pressed && tappable ? theme.line : area.containsMouse && tappable ? theme.chip : theme.inset
    border.width: 1
    border.color: checkable && checked ? theme.soft(tone, 0.45) : theme.hairline
    Behavior on color { ColorAnimation { duration: 120 } }
    activeFocusOnTab: tappable
    Accessible.role: checkable ? Accessible.CheckBox : Accessible.Button
    Accessible.name: title
    Accessible.checkable: checkable
    Accessible.checked: checked
    Keys.onReturnPressed: if (tappable) clicked()
    Keys.onSpacePressed: if (tappable) clicked()

    // What the row does on a tap; under the fold, so the fold opens by itself.
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        enabled: row.tappable
        cursorShape: row.tappable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: row.clicked()
    }

    // The start: a box, an icon or a dot.
    Item {
        id: lead
        x: 14
        y: 14
        width: 36; height: 36
        Rectangle {
            visible: row.checkable
            anchors.centerIn: parent
            width: 24; height: 24; radius: 8
            color: row.checked ? row.tone : "transparent"
            border.width: row.checked ? 0 : 2
            border.color: theme.edge
            Behavior on color { ColorAnimation { duration: 120 } }
            Icon {
                visible: row.checked
                anchors.centerIn: parent
                width: 16; height: 16
                stroke: 2.4
                path: Icons.check
                color: theme.hueInk
            }
        }
        Rectangle {
            visible: !row.checkable && row.standing === "" && row.icon !== ""
            anchors.fill: parent
            radius: 11
            color: theme.soft(row.tone)
            Icon {
                anchors.centerIn: parent
                width: 18; height: 18
                stroke: 2
                path: row.icon
                color: row.tone
            }
        }
        Rectangle {
            visible: !row.checkable && row.standing !== ""
            anchors.centerIn: parent
            width: 14; height: 14; radius: 7
            color: row.dot
            Rectangle {
                anchors.centerIn: parent
                width: 26; height: 26; radius: 13
                color: theme.soft(row.dot, 0.18)
                z: -1
            }
        }
    }

    Column {
        id: rowColumn
        x: 64
        y: 12
        width: parent.width - x - 14 - (trailingText.visible ? trailingText.implicitWidth + 12 : 0) - (row.opens ? 24 : 0)
        spacing: 3
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: row.title
            color: theme.text
            font.pixelSize: 15
            font.weight: Font.DemiBold
            lineHeight: 1.25
        }
        Text {
            visible: text !== ""
            width: parent.width
            wrapMode: Text.WordWrap
            text: row.detail
            color: theme.quiet
            font.pixelSize: 13
            lineHeight: 1.3
        }
        Fold {
            width: parent.width
            title: row.listTitle
            body: row.list
        }
    }

    Text {
        id: trailingText
        visible: text !== ""
        anchors.right: parent.right
        anchors.rightMargin: row.opens ? 38 : 16
        y: 20
        text: row.trailing
        color: row.checkable && !row.checked ? theme.quiet : row.tone
        font.pixelSize: 15
        font.weight: Font.Bold
    }

    Icon {
        visible: row.opens
        anchors.right: parent.right
        anchors.rightMargin: 14
        y: 22
        width: 18; height: 18
        rotation: -90
        path: Icons.chevron
        color: theme.quiet
    }
}
