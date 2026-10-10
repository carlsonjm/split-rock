// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// Genie on this computer before its first use: the one-time download of the
// model that fits this computer, which the person starts.
Rectangle {
    id: panel
    readonly property var model: conversation.localModel
    readonly property string state_: model ? model.state : "missing"
    readonly property bool busy: state_ === "downloading" || state_ === "verifying"
    color: theme.panel
    radius: 16
    implicitHeight: Math.max(196, column.implicitHeight + 36)

    Column {
        id: column
        x: 20; y: 18
        width: parent.width - 40
        spacing: 12

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: panel.state_ === "verifying" ? "Checking the download…"
                : panel.busy ? "Downloading " + conversation.assistantName + "…"
                : "Set up " + conversation.assistantName
            color: theme.text
            font.pixelSize: 16
            font.weight: Font.Bold
        }
        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: panel.busy
                ? "You can keep using the computer. " + productName + " is ready when it finishes, and a question you ask now waits for it."
                : productName + " can answer questions and make everyday changes with no account, and nothing you ask leaves this computer. "
                  + "It needs a one-time download of " + (panel.model ? panel.model.size : "") + "."
            color: theme.text
            font.pixelSize: 15
            lineHeight: 1.3
        }
        Text {
            visible: !panel.busy && panel.model && panel.model.why !== ""
            width: parent.width
            wrapMode: Text.WordWrap
            text: panel.model ? panel.model.why : ""
            color: theme.quiet
            font.pixelSize: 14
            lineHeight: 1.3
        }

        CardProgress {
            barObjectName: "downloadProgress"
            showCount: false
            visible: panel.busy
            done: panel.state_ === "verifying" || !panel.model ? 0 : Math.round(panel.model.progress * 1000)
            total: panel.state_ === "verifying" ? 0 : 1000
            now: panel.state_ === "verifying" ? "Making sure it arrived whole"
                 : panel.model ? Math.round(panel.model.progress * 100) + "% of " + panel.model.size : ""
        }

        Text {
            visible: panel.state_ === "failed"
            width: parent.width
            wrapMode: Text.WordWrap
            text: panel.model ? panel.model.problem : ""
            color: theme.bad
            font.pixelSize: 14
            lineHeight: 1.3
        }

        Flow {
            width: parent.width
            spacing: 8
            topPadding: 4
            Pill {
                objectName: "downloadPill"
                visible: !panel.busy
                filled: true
                text: panel.state_ === "failed" ? "Try again" : "Download"
                onClicked: panel.model.download()
            }
            Pill {
                visible: panel.state_ === "downloading"
                text: "Pause"
                onClicked: panel.model.cancel()
            }
            Pill {
                visible: !panel.busy
                text: "Not now"
                onClicked: conversation.showAssistants()
            }
        }
    }
}
