// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// Choosing an assistant. Split Rock has no AI of its own.
Column {
    spacing: 12

    Text {
        width: parent.width
        text: "Choose an assistant"
        color: theme.text
        font.pixelSize: 16
        font.weight: Font.Bold
    }
    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: (conversation.assistants.length > 0 && conversation.assistants[0].id === "guide"
               ? conversation.assistants[0].name + " works with no account. For anything else, " + productName
                 + " uses an assistant you already have an account with; "
               : productName + " doesn't include an AI of its own. Choose one you have an account with; ")
              + "it signs you in through its own window."
        color: theme.quiet
        font.pixelSize: 14
        lineHeight: 1.3
    }

    Repeater {
        model: conversation.assistants
        Rectangle {
            id: row
            objectName: "assistant-" + modelData.id
            readonly property bool available: modelData.unavailable === ""
            width: parent.width
            height: info.implicitHeight + 28
            radius: 16
            color: theme.panel
            border.width: conversation.assistantId === modelData.id ? 2 : 1
            border.color: conversation.assistantId === modelData.id ? theme.text : theme.line
            opacity: available ? 1 : 0.55
            activeFocusOnTab: available
            Accessible.role: Accessible.Button
            Accessible.name: modelData.name
            Keys.onReturnPressed: conversation.chooseAssistant(modelData.id)

            Column {
                id: info
                x: 18; y: 14
                width: parent.width - 36
                spacing: 4
                Text {
                    text: modelData.name
                    color: theme.text
                    font.pixelSize: 16
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: row.available ? modelData.cost : modelData.unavailable
                    color: theme.quiet
                    font.pixelSize: 14
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: row.available
                cursorShape: Qt.PointingHandCursor
                onClicked: conversation.chooseAssistant(modelData.id)
            }
        }
    }

    Pill {
        visible: conversation.assistantId !== ""
        text: "Back"
        onClicked: conversation.hideAssistants()
    }
}
