// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

Rectangle {
    property string path: Icons.sparkle
    implicitWidth: 32
    implicitHeight: 32
    radius: width * 0.31
    color: theme.jack
    Icon {
        anchors.centerIn: parent
        width: parent.width / 2
        height: width
        path: parent.path
        color: theme.jackInk
    }
}
