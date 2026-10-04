// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The full view, as docs/mockup/Workspace.dc.html draws it: the conversation
// on the left, what the assistant is working on at the right. In Milestone 0
// nothing is kept, so the right side says what this computer is, lists what
// the assistant asked to change and was refused, and says where the notebook's
// parts will be. Choosing an assistant, signing in and the Do it card happen
// in the conversation's column.
Item {
    id: full

    readonly property bool canAsk: conversation.phase === "ready"
    readonly property bool working: conversation.phase === "working"
    property int tab: 0

    function focusField() {
        wideField.forceActiveFocus()
    }

    function send() {
        if (working) {
            conversation.stop()
        } else if (canAsk && wideField.text.trim() !== "") {
            conversation.ask(wideField.text)
            wideField.text = ""
        }
    }

    component Answer: Rectangle {
        property string headline
        property var steps: []
        property var more: []
        property bool waiting: false
        width: Math.min(340, talk.width - 36)
        height: answerColumn.implicitHeight + 24
        radius: 18
        color: theme.panel
        Column {
            id: answerColumn
            x: 14; y: 12
            width: parent.width - 28
            spacing: 8
            Row {
                visible: parent.parent.waiting
                spacing: 8
                Repeater {
                    model: 3
                    Rectangle {
                        width: 7; height: 7; radius: 3.5
                        color: theme.quiet
                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 160 }
                            NumberAnimation { from: 0.25; to: 1; duration: 420 }
                            NumberAnimation { from: 1; to: 0.25; duration: 420 }
                            PauseAnimation { duration: (2 - index) * 160 }
                        }
                    }
                }
            }
            Text {
                visible: text !== ""
                width: parent.width
                wrapMode: Text.WordWrap
                text: parent.parent.headline
                color: theme.text
                font.pixelSize: 15
                font.weight: Font.Bold
                lineHeight: 1.3
            }
            Repeater {
                model: answerColumn.parent.steps
                Text {
                    width: answerColumn.width
                    wrapMode: Text.WordWrap
                    text: (index + 1) + ". " + modelData
                    color: theme.text
                    font.pixelSize: 15
                    lineHeight: 1.3
                }
            }
            Repeater {
                model: answerColumn.parent.more
                Text {
                    width: answerColumn.width
                    wrapMode: Text.WordWrap
                    text: modelData
                    color: theme.text
                    font.pixelSize: 15
                    lineHeight: 1.3
                }
            }
        }
    }

    component Asked: Item {
        property string text
        width: talk.width - 36
        height: askedText.implicitHeight + 20
        Rectangle {
            anchors.right: parent.right
            width: Math.min(300, askedText.implicitWidth + 28)
            height: parent.height
            radius: 18
            color: theme.primary
            Text {
                id: askedText
                x: 14; y: 10
                width: parent.width - 28
                wrapMode: Text.WordWrap
                text: parent.parent.text
                color: theme.primaryInk
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
        }
    }

    component Heading: Text {
        color: theme.quiet
        font.pixelSize: 12
        font.weight: Font.ExtraBold
        font.letterSpacing: 0.7
    }

    component Tile: Rectangle {
        property string label
        property string value
        height: 62
        radius: 12
        color: theme.panel
        Column {
            x: 14; y: 12
            width: parent.width - 28
            spacing: 4
            Text {
                text: parent.parent.label
                color: theme.quiet
                font.pixelSize: 11
                font.weight: Font.ExtraBold
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: parent.parent.value
                color: theme.text
                font.pixelSize: 15
                font.weight: Font.Bold
            }
        }
    }

    component Line: Rectangle {
        property string when
        property string text
        property string status
        width: parent ? parent.width : 0
        height: Math.max(48, lineText.implicitHeight + 24)
        radius: 12
        color: theme.panel
        Text {
            x: 14; y: 13
            width: 86
            visible: text !== ""
            text: parent.when
            color: theme.quiet
            font.pixelSize: 12
            font.weight: Font.Bold
        }
        Text {
            id: lineText
            x: parent.when !== "" ? 114 : 14
            y: 12
            width: parent.width - x - 14 - (statusText.visible ? statusText.implicitWidth + 14 : 0)
            wrapMode: Text.WordWrap
            text: parent.text
            color: theme.text
            font.pixelSize: 15
            lineHeight: 1.3
        }
        Text {
            id: statusText
            anchors.right: parent.right
            anchors.rightMargin: 14
            y: 15
            visible: text !== ""
            text: parent.status
            color: theme.quiet
            font.pixelSize: 12
        }
    }

    // The conversation: what was asked in this window, and its answers.
    Item {
        id: talk
        width: Math.min(420, Math.round(parent.width * 0.42))
        height: parent.height

        Flickable {
            id: history
            width: parent.width
            height: wideBox.y - 12
            contentWidth: width
            contentHeight: bubbles.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)

            Column {
                id: bubbles
                width: parent.width
                spacing: 12

                Text {
                    visible: conversation.turns.length === 0 && conversation.question === "" && !step.item
                    width: talk.width - 36
                    wrapMode: Text.WordWrap
                    text: "Ask " + (conversation.assistantName !== "" ? conversation.assistantName : productName)
                          + " anything about this computer. This conversation ends when the window closes."
                    color: theme.quiet
                    font.pixelSize: 15
                    lineHeight: 1.3
                }

                Repeater {
                    model: conversation.turns
                    Column {
                        spacing: 12
                        Asked { text: modelData.question }
                        Answer {
                            headline: modelData.headline
                            steps: modelData.steps
                            more: modelData.more
                        }
                    }
                }

                Asked {
                    visible: conversation.question !== ""
                    text: conversation.question
                }
                Answer {
                    visible: conversation.question !== "" && !conversation.choosing
                    headline: conversation.headline
                    steps: conversation.steps
                    more: conversation.more
                    waiting: full.working && conversation.headline === "" && conversation.steps.length === 0
                }

                // What is asked of the person now: an assistant to choose, a
                // way to sign in, a change to decide on.
                Loader {
                    id: step
                    objectName: "fullStep"
                    width: talk.width - 18
                    sourceComponent: {
                        if (conversation.choosing)
                            return chooseStep
                        switch (conversation.phase) {
                        case "starting": return startingStep
                        case "signIn":
                        case "signingIn": return signInStep
                        case "problem": return problemStep
                        default: return conversation.cardOpen ? doItStep : null
                        }
                    }
                }

                Text {
                    visible: conversation.notice !== "" && !conversation.choosing
                    width: talk.width - 36
                    wrapMode: Text.WordWrap
                    text: conversation.notice
                    color: theme.text
                    font.pixelSize: 14
                    lineHeight: 1.3
                }
            }
        }

        Rectangle {
            id: wideBox
            y: parent.height - height
            width: parent.width - 18
            height: 52
            radius: 26
            color: theme.panel
            border.width: 1
            border.color: wideField.activeFocus && full.canAsk ? theme.text : theme.edge
            opacity: full.canAsk || full.working ? 1 : 0.5

            TextInput {
                id: wideField
                objectName: "wideField"
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: wideSend.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                color: theme.text
                selectionColor: theme.palette.highlight
                selectedTextColor: theme.palette.highlightedText
                font.pixelSize: 15
                selectByMouse: true
                Accessible.role: Accessible.EditableText
                Accessible.name: "Ask " + productName
                onAccepted: full.send()
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    elide: Text.ElideRight
                    visible: wideField.text === ""
                    text: "Ask, or say what you want done"
                    color: theme.quiet
                    font: wideField.font
                }
            }

            Rectangle {
                id: wideSend
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40; radius: 20
                color: theme.primary
                opacity: full.working || (full.canAsk && wideField.text.trim() !== "") ? 1 : 0.38
                Accessible.role: Accessible.Button
                Accessible.name: full.working ? "Stop" : "Send"
                Icon {
                    anchors.centerIn: parent
                    width: 16; height: 16
                    path: full.working ? Icons.stop : Icons.arrow
                    color: theme.primaryInk
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: full.send()
                }
            }
        }
    }

    Rectangle {
        x: talk.width
        width: 1
        height: parent.height
        color: theme.line
    }

    // What the assistant is working on.
    Flickable {
        id: aside
        x: talk.width + 25
        width: parent.width - x
        height: parent.height
        contentWidth: width
        contentHeight: side.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: side
            width: parent.width
            spacing: 16

            Item {
                width: parent.width
                height: 40
                Row {
                    spacing: 4
                    Repeater {
                        model: ["This computer", "About me", "Kept"]
                        Rectangle {
                            readonly property bool picked: full.tab === index
                            width: tabLabel.implicitWidth + 32
                            height: 40
                            radius: 20
                            color: picked ? theme.primary : tabArea.containsMouse ? theme.chip : "transparent"
                            Accessible.role: Accessible.PageTab
                            Accessible.name: modelData
                            Accessible.selected: picked
                            Text {
                                id: tabLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: parent.picked ? theme.primaryInk : theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                id: tabArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: full.tab = index
                            }
                        }
                    }
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Nothing is kept yet"
                    color: theme.quiet
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            // This computer.
            Column {
                visible: full.tab === 0
                width: parent.width
                spacing: 16

                Row {
                    width: parent.width
                    spacing: 10
                    Tile {
                        width: (parent.width - 20) / 3
                        label: "COMPUTER"
                        value: computerName !== "" ? computerName : "Not reported"
                    }
                    Tile {
                        width: (parent.width - 20) / 3
                        label: "SYSTEM"
                        value: systemName
                    }
                    Tile {
                        width: (parent.width - 20) / 3
                        label: "LAST SNAPSHOT"
                        value: "None yet"
                    }
                }

                Heading { text: "CHANGES" }
                Column {
                    objectName: "changes"
                    width: parent.width
                    spacing: 8
                    Line {
                        visible: conversation.refusedChanges.length === 0
                        text: "Nothing has changed on this computer."
                    }
                    Repeater {
                        model: conversation.refusedChanges
                        Line {
                            when: "Just now"
                            text: modelData.heading
                            status: "Asked, not changed"
                        }
                    }
                }

                Heading { text: "FIXED PROBLEMS" }
                Line {
                    width: parent.width
                    text: "Problems " + productName + " helps fix are written down here, once it has a notebook to keep them in."
                }
            }

            Line {
                visible: full.tab === 1
                width: parent.width
                text: "How you like to be helped, and who helps you. " + productName
                      + " writes this only after you say yes, once it has a notebook to keep it in."
            }

            Line {
                visible: full.tab === 2
                width: parent.width
                text: "Answers you keep, filed by topic. Keep this arrives with the notebook."
            }
        }
    }

    Component { id: chooseStep; AssistantList {} }
    Component { id: startingStep; StartingPanel {} }
    Component { id: signInStep; SignInPanel {} }
    Component { id: problemStep; ProblemPanel {} }
    Component { id: doItStep; DoItCard {} }
}
