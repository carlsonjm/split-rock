// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// The answer, in short steps, as the Just Type mock-up draws it.
Rectangle {
    id: card
    color: theme.panel
    radius: 16
    implicitHeight: Math.max(196, content.implicitHeight + 36)

    readonly property bool working: conversation.phase === "working"
    readonly property bool empty: conversation.question === ""
    readonly property bool hasCard: conversation.card.heading !== undefined

    Column {
        id: content
        x: 20; y: 18
        width: parent.width - 40
        spacing: 12

        // Before the first question.
        Text {
            visible: card.empty
            width: parent.width
            wrapMode: Text.WordWrap
            text: conversation.isGuide ? "Say what's wrong, in your own words."
                                       : "Ask " + conversation.assistantName + " anything about this computer."
            color: theme.text
            font.pixelSize: 16
            font.weight: Font.Bold
        }
        Text {
            visible: card.empty
            width: parent.width
            wrapMode: Text.WordWrap
            text: conversation.isGuide
                  ? conversation.assistantName + " knows the common problems and how to fix them. It needs no account and no internet."
                  : productName + " can't change anything on this computer yet. If " + conversation.assistantName
                    + " asks to, " + productName + " says no and shows you what it wanted."
            color: theme.quiet
            font.pixelSize: 15
            lineHeight: 1.3
        }

        Row {
            visible: card.working && conversation.headline === "" && conversation.steps.length === 0
            spacing: 10
            Repeater {
                model: 3
                Rectangle {
                    width: 8; height: 8; radius: 4
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
            objectName: "headline"
            visible: text !== ""
            width: parent.width
            wrapMode: Text.WordWrap
            text: conversation.headline
            color: theme.text
            font.pixelSize: 16
            font.weight: Font.Bold
            lineHeight: 1.3
        }

        Text {
            visible: text !== ""
            width: parent.width
            wrapMode: Text.WordWrap
            text: conversation.lead
            color: theme.text
            font.pixelSize: 15
            lineHeight: 1.3
        }

        Repeater {
            model: conversation.steps
            Row {
                width: content.width
                spacing: 12
                Rectangle {
                    width: 24; height: 24; radius: 12
                    color: theme.chip
                    y: 1
                    Text {
                        anchors.centerIn: parent
                        text: index + 1
                        color: theme.text
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                    }
                }
                Text {
                    width: parent.width - 36
                    wrapMode: Text.WordWrap
                    text: modelData
                    color: theme.text
                    font.pixelSize: 15
                    lineHeight: 1.3
                }
            }
        }

        Repeater {
            model: conversation.more
            Text {
                width: content.width
                wrapMode: Text.WordWrap
                text: modelData
                color: theme.text
                font.pixelSize: 15
                lineHeight: 1.3
            }
        }

        Flow {
            visible: !card.empty && !card.working
            width: parent.width
            spacing: 8
            topPadding: 4
            Pill {
                objectName: "doItPill"
                visible: card.hasCard
                filled: true
                text: card.hasCard ? "Do it: " + conversation.card.heading.charAt(0).toLowerCase() + conversation.card.heading.slice(1) : ""
                onClicked: conversation.openCard()
            }
            Pill {
                visible: card.hasCard
                text: "Show me how"
                onClicked: conversation.showMeHow()
            }
            Pill {
                objectName: "nextTourPill"
                visible: conversation.nextTour !== ""
                filled: true
                text: "Next: " + conversation.nextTour
                onClicked: conversation.showTour()
            }
            Pill {
                text: "New question"
                onClicked: conversation.newQuestion()
            }
        }
    }
}
