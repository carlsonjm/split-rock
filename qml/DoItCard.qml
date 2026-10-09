// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Do it card, as the mock-up draws it. A change the assistant proposed to
// Split Rock is made by Do it; a request through the assistant's own tools was
// refused before the card appeared, and Do it is unavailable.
Column {
    id: root
    spacing: 14

    // How it would be undone, and whether it needs the password: an icon
    // and the card's own words.
    component Fact: Row {
        property string icon
        property string label
        property string value
        width: root.width - 80
        spacing: 12
        Icon {
            y: 2
            width: 18; height: 18
            path: parent.icon
            color: theme.accent
        }
        Text {
            width: parent.width - 30
            wrapMode: Text.WordWrap
            text: "<b>" + parent.label + "</b>  " + parent.value.replace(/&/g, "&amp;").replace(/</g, "&lt;")
            textFormat: Text.StyledText
            color: theme.text
            font.pixelSize: 15
            lineHeight: 1.35
        }
    }

    Rectangle {
        objectName: "doItCard"
        width: parent.width
        height: article.implicitHeight + 48
        radius: theme.cardRadius
        color: theme.raised
        border.width: 1
        border.color: theme.hairline

        Column {
            id: article
            x: 24; y: 24
            width: parent.width - 48
            spacing: 18

            Row {
                spacing: 14
                Rectangle {
                    width: 44; height: 44
                    radius: 14
                    color: theme.accentSoft
                    Icon {
                        anchors.centerIn: parent
                        width: 22; height: 22
                        stroke: 2
                        path: Icons.forKind(conversation.card.kind || "")
                        color: theme.accent
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: article.width - 58
                    spacing: 3
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: conversation.card.heading || ""
                        color: theme.text
                        font.pixelSize: 22
                        font.weight: Font.Bold
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        visible: text !== ""
                        text: conversation.card.whatChanges || ""
                        color: theme.quiet
                        font.pixelSize: 15
                        lineHeight: 1.3
                    }
                }
            }

            Text {
                width: article.width
                wrapMode: Text.WordWrap
                visible: text !== ""
                text: conversation.card.why || ""
                color: theme.text
                font.pixelSize: 16
                lineHeight: 1.45
            }

            Rectangle {
                width: article.width
                height: facts.implicitHeight + 32
                radius: 16
                color: theme.sheet
                Column {
                    id: facts
                    x: 16; y: 16
                    spacing: 12
                    Fact { icon: Icons.shield; label: "Undo"; value: conversation.card.undo || "" }
                    Fact { icon: Icons.lock; label: "Password"; value: conversation.card.password || "" }
                }
            }

            Fold {
                width: article.width
                body: conversation.card.details || ""
            }

            Flow {
                width: article.width
                spacing: 10
                Pill {
                    objectName: "doItButton"
                    implicitHeight: 52
                    padding: 28
                    fontSize: 16
                    filled: true
                    enabled: conversation.card.available === true && !conversation.card.making
                    text: conversation.card.making ? "Making it…" : "Do it"
                    onClicked: conversation.doIt()
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
            Text {
                width: article.width
                visible: text !== ""
                wrapMode: Text.WordWrap
                text: conversation.card.unavailable || ""
                color: theme.quiet
                font.pixelSize: 14
            }
        }
    }
}
