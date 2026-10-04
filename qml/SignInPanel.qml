// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// Signing in to the chosen assistant. Each assistant signs the person in
// through its own window; Jack only offers the ways that need no terminal and
// no key held by Jack.
Rectangle {
    color: theme.panel
    radius: 16
    implicitHeight: Math.max(196, column.implicitHeight + 36)
    readonly property bool waiting: jack.phase === "signingIn"

    Column {
        id: column
        x: 20; y: 18
        width: parent.width - 40
        spacing: 12

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: waiting ? "Finish signing in to " + jack.assistantName + " in the window that opened."
                          : "Sign in to " + jack.assistantName
            color: theme.text
            font.pixelSize: 16
            font.weight: Font.Bold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: waiting ? "Jack is waiting. It never sees your password, your account or your bill."
                          : jack.assistantCost
            color: theme.text
            font.pixelSize: 15
            lineHeight: 1.3
        }
        Text {
            visible: !waiting
            width: parent.width
            wrapMode: Text.WordWrap
            text: jack.assistantName + " signs you in through its own window. Jack never sees your password, your account or your bill."
            color: theme.quiet
            font.pixelSize: 14
            lineHeight: 1.3
        }
        Text {
            visible: waiting && jack.signingInWithoutTerminal
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
                model: jack.signInChoices
                Pill {
                    objectName: "signIn-" + modelData.id
                    filled: index === 0
                    text: modelData.name
                    onClicked: jack.signIn(modelData.id)
                }
            }
        }

        Flow {
            visible: waiting
            width: parent.width
            spacing: 8
            topPadding: 4
            Pill {
                visible: jack.signInPage !== ""
                filled: true
                text: "Open the sign-in page again"
                onClicked: Qt.openUrlExternally(jack.signInPage)
            }
            Pill {
                objectName: "cancelSignIn"
                text: "Cancel"
                onClicked: jack.cancelSignIn()
            }
        }

        Fold {
            visible: !waiting && jack.notice !== "" && body !== ""
            width: parent.width
            body: jack.problemDetails
        }

        Fold {
            visible: !waiting && jack.notOffered.length > 0
            width: parent.width
            title: "Not offered here (" + jack.notOffered.length + ")"
            body: jack.notOffered.map(m => m.name + ": " + m.whyNot).join("\n")
        }
    }
}
