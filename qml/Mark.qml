// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// The assistant's mark in its rounded tile, laid out as on the app icon. The
// drawing is the one file every surface uses, assets/mark.svg; a path given here
// draws a line icon in the same tile instead.
Rectangle {
    id: tile
    property string path: markPath
    property real stroke: 1.75
    readonly property bool isMark: path === markPath
    implicitWidth: 32
    implicitHeight: 32
    radius: width * 0.156
    color: theme.mark
    Icon {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: tile.isMark ? tile.width * 0.027 : 0
        width: tile.isMark ? tile.width * 0.77 : tile.width / 2
        height: width
        path: tile.path
        stroke: tile.stroke
        filled: tile.isMark
        color: theme.markInk
    }
}
