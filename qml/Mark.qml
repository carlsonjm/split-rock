// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// The assistant's mark in its rounded tile. The drawing is the one file every
// surface uses, assets/mark.svg; a path given here draws something else in the
// same tile instead.
Rectangle {
    property string path: markPath
    property real stroke: path === markPath ? 2 : 1.75
    implicitWidth: 32
    implicitHeight: 32
    radius: width * 0.31
    color: theme.mark
    Icon {
        anchors.centerIn: parent
        width: parent.width / 2
        height: width
        path: parent.path
        stroke: parent.stroke
        color: theme.markInk
    }
}
