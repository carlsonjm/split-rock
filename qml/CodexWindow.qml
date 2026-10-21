// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import "Icons.js" as Icons

// Ask Codex (docs/ASK.md, Codex): one conversation with Codex in a window of
// its own, drawn like a Helper, that a card workspace places like any other.
// Everything Codex says is under its name; a change it proposes waits on a
// Do it row; Keep as a note keeps the conversation, and closing forgets it.
Window {
    id: codexWindow
    objectName: "codexWindow"
    title: "Ask Codex"
    color: theme.sheet
    width: 560
    height: 680
    minimumWidth: 360
    minimumHeight: 420
    visible: false

    Theme { id: theme }

    readonly property string phase: chat.phase
    readonly property bool talking: phase === "ready" || phase === "working"

    onClosing: chat.end()

    Flickable {
        id: scroller
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: foot.top
        contentHeight: frame.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // The newest words stay in sight as they come.
        onContentHeightChanged: if (contentHeight > height) contentY = contentHeight - height

        WheelScroll { flickable: scroller }

        CardFrame {
            id: frame
            width: scroller.width

            CardHead {
                icon: Icons.chat
                heading: "Ask Codex"
                sub: "With your ChatGPT plan · closes when you're done"
            }

            Column {
                objectName: "codex-turns"
                width: frame.innerWidth
                spacing: 8
                Repeater {
                    model: chat.turns
                    Column {
                        id: turn
                        required property var modelData
                        readonly property bool mine: modelData.who === "you"
                        width: frame.innerWidth
                        spacing: 4
                        // Who is speaking, every time it is Codex.
                        Text {
                            visible: !turn.mine
                            text: "Codex"
                            color: theme.quiet
                            font.pixelSize: 12
                        }
                        Rectangle {
                            objectName: turn.mine ? "codex-turn-you" : "codex-turn-codex"
                            anchors.right: turn.mine ? parent.right : undefined
                            width: Math.min(frame.innerWidth * 0.85, said.implicitWidth + 28)
                            height: said.implicitHeight + 20
                            radius: 12
                            color: turn.mine ? theme.raised : theme.inset
                            Text {
                                id: said
                                x: 14; y: 10
                                width: Math.min(frame.innerWidth * 0.85 - 28, implicitWidth)
                                text: turn.modelData.text
                                color: theme.text
                                font.pixelSize: 15
                                lineHeight: 1.3
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                            }
                        }
                    }
                }
                Text {
                    visible: codexWindow.phase === "working"
                    text: "Codex is thinking…"
                    color: theme.quiet
                    font.pixelSize: 13
                }
            }

            // A change Codex proposed: nothing happens until Do it.
            CardRow {
                objectName: "codex-proposal"
                visible: chat.proposal.title !== undefined
                icon: Icons.sliders
                reserve: proposalPills.implicitWidth + 12
                title: chat.proposal.title || ""
                detail: {
                    const state = chat.proposal.state
                    if (state === "making") return "Making the change…"
                    if (state === "done") return "Done"
                    if (state === "failed") return "That didn't work. Nothing else was changed."
                    if (state === "undoing") return "Putting it back…"
                    if (state === "undone") return "Put back as it was"
                    if (state === "undoFailed") return "Putting it back didn't work"
                    return (chat.proposal.what ? chat.proposal.what + " · " : "") + (chat.proposal.undoWay || "")
                }
                Row {
                    id: proposalPills
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Pill {
                        objectName: "codex-not-now"
                        visible: chat.proposal.state === "offered"
                        implicitHeight: 32
                        padding: 12
                        fontSize: 13
                        text: "Not now"
                        onClicked: chat.notNow()
                    }
                    Pill {
                        objectName: "codex-do-it"
                        visible: chat.proposal.state === "offered"
                        implicitHeight: 32
                        padding: 12
                        fontSize: 13
                        filled: true
                        text: "Do it"
                        onClicked: chat.doIt()
                    }
                    Pill {
                        objectName: "codex-undo"
                        visible: chat.proposal.canUndo === true
                        implicitHeight: 32
                        padding: 12
                        fontSize: 13
                        text: "Undo"
                        onClicked: chat.undo()
                    }
                }
            }

            // Signing in, through Codex's own page in the browser.
            Column {
                objectName: "codex-sign-in"
                visible: ["starting", "signIn", "signingIn", "signedIn", "problem", "unavailable"].indexOf(codexWindow.phase) >= 0
                width: frame.innerWidth
                spacing: 12
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: {
                        switch (codexWindow.phase) {
                        case "starting": return "Starting Codex…"
                        case "signIn": return "Sign in to Codex with ChatGPT"
                        case "signingIn": return "Finish signing in to ChatGPT in your browser."
                        case "signedIn": return "Signed in. Codex is ready for your bigger questions."
                        }
                        return chat.problem
                    }
                    color: theme.text
                    font.pixelSize: 15
                    font.weight: Font.Medium
                    lineHeight: 1.3
                }
                Text {
                    visible: codexWindow.phase === "signIn" || codexWindow.phase === "signingIn"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: "Codex signs you in through its own page, and your ChatGPT plan pays for what it does. "
                        + productTitle + " never sees your password, your account or your bill."
                    color: theme.quiet
                    font.pixelSize: 13
                    lineHeight: 1.3
                }
                Flow {
                    width: parent.width
                    spacing: 8
                    Pill {
                        objectName: "codex-sign-in-button"
                        visible: codexWindow.phase === "signIn"
                        filled: true
                        text: chat.signInName !== "" ? chat.signInName : "Sign in"
                        onClicked: chat.signIn()
                    }
                    Pill {
                        visible: codexWindow.phase === "signingIn" && chat.signInPage !== ""
                        text: "Open the sign-in page again"
                        onClicked: Qt.openUrlExternally(chat.signInPage)
                    }
                }
            }

            Text {
                objectName: "codex-notice"
                visible: text !== ""
                width: frame.innerWidth
                wrapMode: Text.WordWrap
                text: chat.notice
                color: theme.quiet
                font.pixelSize: 13
            }
        }
    }

    // What to ask next, and the way out.
    Column {
        id: foot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 12

        Rectangle {
            visible: codexWindow.talking || codexWindow.phase === "signedIn"
            width: parent.width
            height: 44
            radius: 22
            color: theme.inset
            TextInput {
                id: field
                objectName: "codex-field"
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                verticalAlignment: TextInput.AlignVCenter
                color: theme.text
                font.pixelSize: 15
                clip: true
                enabled: codexWindow.phase !== "working"
                onAccepted: {
                    chat.ask(text)
                    text = ""
                }
                Text {
                    visible: field.text === ""
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Ask Codex something else"
                    color: theme.quiet
                    font.pixelSize: 15
                }
            }
        }

        Flow {
            width: parent.width
            spacing: 8
            Pill {
                objectName: "codex-keep"
                visible: chat.turns.length > 0
                // Once kept, it opens the note in Notes, which outlives this
                // window.
                enabled: chat.kept ? chat.keptOpens : codexWindow.phase !== "working"
                text: chat.kept ? (chat.keptOpens ? "Open the note" : "Kept as a note") : "Keep as a note"
                onClicked: chat.kept ? chat.openKept() : chat.keep()
            }
            Pill {
                objectName: "codex-stop"
                visible: codexWindow.phase === "working"
                text: "Stop"
                onClicked: chat.stop()
            }
            Pill {
                objectName: "codex-close"
                text: "Close"
                onClicked: codexWindow.close()
            }
        }
    }
}
