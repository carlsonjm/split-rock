// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// The assistant could not start or stopped: what happened, and the ways on.
Rectangle {
    color: theme.panel
    radius: 16
    implicitHeight: Math.max(196, problemColumn.implicitHeight + 36)
    Column {
        id: problemColumn
        x: 20; y: 18
        width: parent.width - 40
        spacing: 12
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: conversation.problem
            color: theme.text
            font.pixelSize: 16
            font.weight: Font.Bold
        }
        Fold {
            width: parent.width
            body: conversation.problemDetails
        }
        Flow {
            width: parent.width
            spacing: 8
            Pill {
                filled: true
                text: "Try again"
                onClicked: conversation.chooseAssistant(conversation.assistantId)
            }
            Pill {
                text: "Choose another assistant"
                onClicked: conversation.showAssistants()
            }
        }
    }
}
