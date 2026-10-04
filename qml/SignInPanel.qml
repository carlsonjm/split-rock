// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// Signing in to the chosen assistant. Each assistant signs the person in
// through its own window; only the ways that need no terminal and no key held
// by Split Rock are offered.
Rectangle {
    id: panel
    // What a chosen way to sign in does. The card hands over to the full
    // window first, so the assistant's browser can come in front.
    property var signInWith: id => conversation.signIn(id)
    color: theme.panel
    radius: 16
    implicitHeight: Math.max(196, column.implicitHeight + 36)
    readonly property bool waiting: conversation.phase === "signingIn"

    Column {
        id: column
        x: 20; y: 18
        width: parent.width - 40
        spacing: 12

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: waiting ? "Finish signing in to " + conversation.assistantName + " in the window that opened."
                          : "Sign in to " + conversation.assistantName
            color: theme.text
            font.pixelSize: 16
            font.weight: Font.Bold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: waiting ? productName + " is waiting. It never sees your password, your account or your bill."
                          : conversation.assistantCost
            color: theme.text
            font.pixelSize: 15
            lineHeight: 1.3
        }
        Text {
            visible: !waiting
            width: parent.width
            wrapMode: Text.WordWrap
            text: conversation.assistantName + " signs you in through its own window. " + productName + " never sees your password, your account or your bill."
            color: theme.quiet
            font.pixelSize: 14
            lineHeight: 1.3
        }
        Text {
            visible: waiting && conversation.signingInWithoutTerminal
            width: parent.width
            wrapMode: Text.WordWrap
            text: "If the page gives you a code to paste somewhere, this way of signing in needs a terminal. Tap Cancel."
            color: theme.quiet
            font.pixelSize: 14
            lineHeight: 1.3
        }

        Flow {
            visible: !waiting
            width: parent.width
            spacing: 8
            topPadding: 4
            Repeater {
                model: conversation.signInChoices
                Pill {
                    objectName: "signIn-" + modelData.id
                    filled: index === 0
                    text: modelData.name
                    onClicked: panel.signInWith(modelData.id)
                }
            }
        }

        Flow {
            visible: waiting
            width: parent.width
            spacing: 8
            topPadding: 4
            Pill {
                visible: conversation.signInPage !== ""
                filled: true
                text: "Open the sign-in page again"
                onClicked: Qt.openUrlExternally(conversation.signInPage)
            }
            Pill {
                objectName: "cancelSignIn"
                text: "Cancel"
                onClicked: conversation.cancelSignIn()
            }
        }

        Fold {
            visible: !waiting && conversation.notice !== "" && body !== ""
            width: parent.width
            body: conversation.problemDetails
        }

        Fold {
            visible: !waiting && conversation.notOffered.length > 0
            width: parent.width
            title: "Not offered here (" + conversation.notOffered.length + ")"
            body: conversation.notOffered.map(m => m.name + ": " + m.whyNot).join("\n")
        }
    }
}
