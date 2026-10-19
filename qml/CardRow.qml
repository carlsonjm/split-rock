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
    // What a tap does, quiet after the trailing words, such as Remove.
    property string action
    property color tone: theme.accent
    property string icon
    // A box to tick, instead of the icon.
    property bool checkable: false
    property bool checked: false
    // Opens something: a chevron at its end.
    property bool opens: false
    // Does something in place, as its trailing words say, with no chevron.
    property bool acts: false
    // good, warn or bad: a dot instead of the icon.
    property string standing
    // What it holds, folded under it.
    property string list
    property string listTitle: "Show which"
    signal clicked()

    readonly property bool tappable: checkable || opens || acts
    readonly property color dot: standing === "bad" ? theme.bad : standing === "warn" ? theme.warn : theme.good
    width: parent ? parent.width : 0
    height: Math.max(56, rowColumn.implicitHeight + 24)
    radius: theme.cardRadius
    color: area.pressed && tappable ? theme.line : area.containsMouse && tappable ? theme.chip : theme.inset
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
        x: 12
        y: 12
        width: 32; height: 32
        Rectangle {
            visible: row.checkable
            anchors.centerIn: parent
            width: 20; height: 20; radius: 4
            color: row.checked ? row.tone : "transparent"
            border.width: row.checked ? 0 : 2
            border.color: theme.edge
            Behavior on color { ColorAnimation { duration: 120 } }
            Icon {
                visible: row.checked
                anchors.centerIn: parent
                width: 14; height: 14
                stroke: 2.4
                path: Icons.check
                color: theme.hueInk
            }
        }
        Rectangle {
            visible: !row.checkable && row.standing === "" && row.icon !== ""
            anchors.fill: parent
            radius: theme.cardRadius
            color: theme.inset
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
            width: 10; height: 10; radius: 5
            color: row.dot
        }
    }

    Column {
        id: rowColumn
        x: 56
        y: 12
        width: parent.width - x - 12 - (trailingRow.implicitWidth > 0 ? trailingRow.implicitWidth + 12 : 0) - (row.opens ? 24 : 0)
        spacing: 3
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: row.title
            color: theme.text
            font.pixelSize: 15
            font.weight: Font.Medium
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

    Row {
        id: trailingRow
        anchors.right: parent.right
        anchors.rightMargin: row.opens ? 36 : 12
        y: 18
        spacing: 12
        Text {
            id: trailingText
            visible: text !== ""
            text: row.trailing
            color: row.checkable && !row.checked ? theme.quiet : row.tone
            font.pixelSize: 13
            font.weight: row.action !== "" ? Font.DemiBold : Font.Medium
        }
        Text {
            visible: text !== ""
            text: row.action
            color: theme.quiet
            font.pixelSize: 13
        }
    }

    Icon {
        visible: row.opens
        anchors.right: parent.right
        anchors.rightMargin: 12
        y: 19
        width: 18; height: 18
        rotation: -90
        path: Icons.chevron
        color: theme.quiet
    }
}
