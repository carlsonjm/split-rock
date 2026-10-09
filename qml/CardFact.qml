// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A fact under a card's heading: an icon and a few words.
Row {
    id: fact
    property string icon
    property string label
    property color tone: theme.quiet
    spacing: 7
    Icon {
        anchors.verticalCenter: parent.verticalCenter
        width: 15; height: 15
        path: fact.icon
        color: fact.tone
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: fact.label
        color: theme.quiet
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }
}
