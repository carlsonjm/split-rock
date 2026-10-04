// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// What the assistant offers to remember about the person, asked as Split
// Rock's own question under the answer. Nothing is written until Remember.
Column {
    objectName: "rememberLine"
    spacing: 8
    topPadding: 4

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        textFormat: Text.StyledText
        text: "<b>Remember that?</b> " + conversation.remember.replace(/&/g, "&amp;").replace(/</g, "&lt;")
        color: theme.text
        font.pixelSize: 15
        lineHeight: 1.3
    }
    Flow {
        width: parent.width
        spacing: 8
        Pill {
            objectName: "rememberPill"
            implicitHeight: 40
            filled: true
            text: "Remember"
            onClicked: conversation.rememberYes()
        }
        Pill {
            implicitHeight: 40
            text: "Not now"
            onClicked: conversation.rememberNo()
        }
    }
}
