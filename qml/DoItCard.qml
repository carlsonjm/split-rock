// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Do it card, as the mock-up draws it. In Milestone 0 the change it shows
// was refused before the card appeared, and Do it is unavailable.
Column {
    id: root
    spacing: 14

    Rectangle {
        anchors.right: parent.right
        width: Math.min(420, bubble.implicitWidth + 28)
        height: bubble.implicitHeight + 20
        radius: 18
        color: theme.primary
        visible: conversation.question !== ""
        Text {
            id: bubble
            x: 14; y: 10
            width: parent.width - 28
            wrapMode: Text.WordWrap
            text: conversation.question
            color: theme.primaryInk
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }
    }

    Rectangle {
        objectName: "doItCard"
        width: parent.width
        height: article.implicitHeight + 38
        radius: 18
        color: theme.panel
        border.width: 1
        border.color: theme.line

        Column {
            id: article
            x: 22; y: 20
            width: parent.width - 44

            Row {
                spacing: 12
                bottomPadding: 8
                Mark {
                    width: 40; height: 40
                    radius: 12
                    path: Icons.forKind(conversation.card.kind || "")
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: article.width - 52
                    wrapMode: Text.WordWrap
                    text: conversation.card.heading || ""
                    color: theme.text
                    font.pixelSize: 22
                    font.weight: Font.ExtraBold
                }
            }

            Repeater {
                model: [
                    { label: "WHAT CHANGES", value: conversation.card.whatChanges },
                    { label: "WHY", value: conversation.card.why },
                    { label: "UNDO", value: conversation.card.undo },
                    { label: "PASSWORD", value: conversation.card.password }
                ]
                Item {
                    width: article.width
                    height: Math.max(labelText.implicitHeight, valueText.implicitHeight) + 28
                    Rectangle { width: parent.width; height: 1; color: theme.line }
                    Text {
                        id: labelText
                        y: 17
                        width: 150
                        text: modelData.label
                        color: theme.quiet
                        font.pixelSize: 12
                        font.weight: Font.ExtraBold
                        font.letterSpacing: 0.7
                    }
                    Text {
                        id: valueText
                        x: 166; y: 14
                        width: parent.width - 166
                        wrapMode: Text.WordWrap
                        text: modelData.value || ""
                        color: theme.text
                        font.pixelSize: 16
                        lineHeight: 1.3
                    }
                }
            }

            Rectangle { width: article.width; height: 1; color: theme.line }
            Item { width: 1; height: 12 }
            Fold {
                width: article.width
                body: conversation.card.details || ""
            }

            Item { width: 1; height: 18 }
            Flow {
                width: article.width
                spacing: 10
                Pill {
                    objectName: "doItButton"
                    implicitHeight: 52
                    padding: 28
                    fontSize: 16
                    filled: true
                    enabled: false
                    text: "Do it"
                }
                Pill {
                    objectName: "showMeHowButton"
                    implicitHeight: 52
                    padding: 22
                    fontSize: 15
                    text: "Show me how instead"
                    onClicked: conversation.showMeHow()
                }
                Pill {
                    objectName: "notNowButton"
                    implicitHeight: 52
                    padding: 22
                    fontSize: 15
                    text: "Not now"
                    onClicked: conversation.notNow()
                }
            }
            Item { width: 1; height: 10 }
            Text {
                width: article.width
                wrapMode: Text.WordWrap
                text: productName + " can't make changes yet, so nothing was changed."
                color: theme.quiet
                font.pixelSize: 14
            }
        }
    }
}
