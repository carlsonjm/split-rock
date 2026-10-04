// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// While the chosen assistant starts.
Rectangle {
    color: theme.panel
    radius: 16
    implicitHeight: 196
    Text {
        x: 20; y: 18
        text: "Starting " + conversation.assistantName + "…"
        color: theme.text
        font.pixelSize: 16
        font.weight: Font.Bold
    }
}
