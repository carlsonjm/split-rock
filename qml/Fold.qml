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
    // Opens at its end, as for what an update printed; a list of what a row
    // holds opens at its start instead, so its first names show.
    property bool fromEnd: true
    // The same list by the names people know, where there are some: each
    // {name, note, size}, and for the system's own parts their list as parts,
    // shown in place of the body.
    property var entries: []
    readonly property bool named: !fold.fromEnd && fold.entries.length > 0
    spacing: 8
    visible: body !== "" || fold.entries.length > 0

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
        height: Math.min(scroller.contentHeight, fold.longest) + 24
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
            contentHeight: fold.fromEnd ? detail.implicitHeight : fold.named ? entryColumn.implicitHeight : names.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            function settle() {
                contentY = fold.fromEnd ? Math.max(0, contentHeight - height) : 0
            }
            onContentHeightChanged: settle()
            onVisibleChanged: if (visible) settle()
            // What was printed, to select and copy. A long one draws only
            // the part in view, so a list, read top to bottom, is plain text.
            TextEdit {
                id: detail
                visible: fold.fromEnd
                width: scroller.width
                text: fold.fromEnd ? fold.body : ""
                readOnly: true
                selectByMouse: true
                wrapMode: Text.WrapAnywhere
                color: theme.text
                font.family: theme.mono
                font.pixelSize: 13
            }
            Column {
                id: entryColumn
                visible: fold.named
                width: scroller.width
                Repeater {
                    model: fold.named ? fold.entries : []
                    Column {
                        id: entry
                        property bool partsOpen: false
                        width: entryColumn.width
                        Item {
                            width: entry.width
                            height: Math.max(32, entryName.implicitHeight + 10)
                            Text {
                                id: entryName
                                anchors.left: parent.left
                                anchors.right: entrySize.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                wrapMode: Text.WordWrap
                                textFormat: Text.StyledText
                                text: modelData.name.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                      + (modelData.note ? "<font color=\"" + theme.quiet + "\"> · " + modelData.note + "</font>" : "")
                                color: theme.text
                                font.pixelSize: 13
                            }
                            Text {
                                id: entrySize
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.size || ""
                                color: theme.quiet
                                font.pixelSize: 13
                            }
                        }
                        // The system's own parts open to their full list.
                        Item {
                            visible: (modelData.parts || "") !== ""
                            width: partsToggle.implicitWidth + 12
                            height: 26
                            Accessible.role: Accessible.Button
                            Accessible.name: partsToggle.text
                            Text {
                                id: partsToggle
                                anchors.verticalCenter: parent.verticalCenter
                                text: (entry.partsOpen ? "▾ " : "▸ ") + "Show what's included"
                                color: theme.quiet
                                font.pixelSize: 13
                                font.weight: Font.Medium
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: entry.partsOpen = !entry.partsOpen
                            }
                        }
                        Text {
                            visible: entry.partsOpen
                            width: entry.width
                            topPadding: 4
                            bottomPadding: 6
                            text: modelData.parts || ""
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAnywhere
                            color: theme.text
                            font.family: theme.mono
                            font.pixelSize: 12
                            lineHeight: 1.4
                        }
                    }
                }
            }
            Text {
                id: names
                visible: !fold.fromEnd && !fold.named
                width: scroller.width
                text: fold.fromEnd ? "" : fold.body
                textFormat: Text.PlainText
                wrapMode: Text.WrapAnywhere
                color: theme.text
                font.family: theme.mono
                font.pixelSize: 13
            }
        }
    }

    // The way back to the card from a long one.
    Item {
        visible: fold.open && scroller.contentHeight > fold.longest
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
