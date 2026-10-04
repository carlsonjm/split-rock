// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import "Icons.js" as Icons

// Milestone 0's window: the Just Type sheet. One field, already focused;
// answers in short steps; a refused change shown as the Do it card.
Window {
    id: window
    width: 740
    height: 680
    minimumWidth: 480
    minimumHeight: 460
    visible: true
    title: productTitle
    color: theme.sheet

    Theme { id: theme }

    readonly property bool canAsk: conversation.phase === "ready"
    readonly property bool working: conversation.phase === "working"
    // The Do it card has the sheet to itself, as in the mock-up.
    readonly property bool cardMode: !conversation.choosing && conversation.cardOpen && (canAsk || working)

    Shortcut {
        sequence: "Esc"
        enabled: window.cardMode
        onActivated: conversation.notNow()
    }
    onCardModeChanged: if (!cardMode) field.forceActiveFocus()

    // The field shows the question being answered, however it was asked.
    Connections {
        target: conversation
        function onAnswerChanged() {
            if (conversation.question !== "" && conversation.question !== "Show me how instead" && conversation.question !== field.text)
                field.text = conversation.question
        }
    }

    function send() {
        if (working) {
            conversation.stop()
        } else if (canAsk && field.text.trim() !== "") {
            conversation.ask(field.text)
        }
    }

    Flickable {
        id: page
        anchors.fill: parent
        contentWidth: width
        contentHeight: sheet.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: sheet
            x: 22; y: 18
            width: page.width - 44
            spacing: 14

            // The assistant's name, and which assistant is answering.
            Item {
                visible: !window.cardMode
                width: parent.width
                height: 40
                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Mark { anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: productName
                        color: theme.text
                        font.pixelSize: 15
                        font.weight: Font.ExtraBold
                    }
                }
                Pill {
                    objectName: "assistantPill"
                    anchors.right: parent.right
                    implicitHeight: 40
                    padding: 14
                    fontSize: 13
                    visible: conversation.assistantId !== "" && !conversation.choosing
                    enabled: conversation.phase !== "working" && conversation.phase !== "signingIn"
                    text: conversation.assistantName + "  ▾"
                    onClicked: conversation.showAssistants()
                }
            }

            // The one field.
            Rectangle {
                visible: !window.cardMode
                width: parent.width
                height: 60
                radius: 30
                color: theme.panel
                opacity: window.canAsk || window.working ? 1 : 0.5
                border.width: 1
                border.color: field.activeFocus && window.canAsk ? theme.text : theme.edge

                TextInput {
                    id: field
                    objectName: "field"
                    anchors.left: parent.left
                    anchors.leftMargin: 22
                    anchors.right: sendButton.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    focus: true
                    clip: true
                    color: theme.text
                    selectionColor: theme.palette.highlight
                    selectedTextColor: theme.palette.highlightedText
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    selectByMouse: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: "Ask " + productName
                    onAccepted: window.send()
                    Keys.onEscapePressed: conversation.stop()

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: field.text === ""
                        text: "Ask, or say what you want done"
                        color: theme.quiet
                        font: field.font
                    }
                }

                Rectangle {
                    id: sendButton
                    objectName: "sendButton"
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 44; radius: 22
                    color: theme.primary
                    opacity: window.working || (window.canAsk && field.text.trim() !== "") ? 1 : 0.38
                    Accessible.role: Accessible.Button
                    Accessible.name: window.working ? "Stop" : "Send"
                    Icon {
                        anchors.centerIn: parent
                        width: 16; height: 16
                        path: window.working ? Icons.stop : Icons.arrow
                        color: theme.primaryInk
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.send()
                    }
                }
            }

            // Questions to start from.
            Flow {
                width: parent.width
                spacing: 8
                visible: !conversation.choosing && !window.cardMode && (conversation.phase === "ready" || conversation.phase === "working")
                Repeater {
                    model: conversation.suggestions
                    Pill {
                        implicitHeight: 40
                        padding: 16
                        readonly property bool picked: conversation.question === modelData
                        filled: picked
                        enabled: window.canAsk
                        text: modelData
                        onClicked: {
                            field.text = modelData
                            conversation.ask(modelData)
                        }
                    }
                }
            }

            // What fills the sheet below the field.
            Loader {
                id: body
                width: parent.width
                sourceComponent: {
                    if (conversation.choosing)
                        return assistantList
                    switch (conversation.phase) {
                    case "starting": return starting
                    case "signIn":
                    case "signingIn": return signInPanel
                    case "problem": return problemPanel
                    default: return conversation.cardOpen ? doItCard : answerCard
                    }
                }
            }

            Text {
                objectName: "notice"
                visible: conversation.notice !== "" && !conversation.choosing
                width: parent.width
                wrapMode: Text.WordWrap
                text: conversation.notice
                color: theme.text
                font.pixelSize: 14
                lineHeight: 1.3
            }
        }
    }

    Component { id: assistantList; AssistantList {} }
    Component { id: answerCard; AnswerCard {} }
    Component { id: doItCard; DoItCard {} }
    Component { id: signInPanel; SignInPanel {} }
    Component {
        id: starting
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
    }
    Component {
        id: problemPanel
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
    }
}
