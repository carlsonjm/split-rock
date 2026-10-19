// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Health check card: each part of the computer as good, worth a look or a
// problem, with the card that helps, or the problem described for someone to
// help with (src/app/HealthCheck.h).
CardFrame {
    id: card
    objectName: "healthCard"
    readonly property var job: helpers.health
    readonly property string stage: card.job.stage
    readonly property var items: card.job.items
    readonly property int bad: card.items.filter(i => i.state === "bad").length
    readonly property int warn: card.items.filter(i => i.state === "warn").length
    readonly property int good: card.items.filter(i => i.state === "good").length
    hue: card.stage === "done" && card.bad > 0 ? "coral" : card.stage === "done" && card.warn > 0 ? "amber" : "green"

    // The row whose description was copied last, to say so for a moment.
    property string copied: ""

    function act(item) {
        if (item.action === "ask") {
            clipboard.text = card.job.helpText(item.question)
            clipboard.selectAll()
            clipboard.copy()
            card.copied = item.id
            copiedFor.restart()
        } else if (item.action === "restart") {
            card.job.restart()
        } else if (item.action === "downloads") {
            card.job.openDownloads()
        } else if (item.action !== "") {
            helpers.openChore(item.action)
        }
    }

    CardHead {
        headingObjectName: "healthHeading"
        tone: card.tone
        icon: Icons.pulse
        spinning: false
        heading: card.stage === "looking" ? "Checking this computer"
               : card.job.attention === 0 ? "All good"
               : card.job.attention === 1 ? "1 thing to look at" : card.job.attention + " things to look at"
        sub: card.stage === "looking" ? "It only reads; nothing changes."
           : "Checked " + card.job.lastChecked.toLowerCase() + "." + (card.job.watched ? " It runs by itself once a week." : "")
    }

    // The clipboard, through an edit nobody sees.
    TextEdit {
        id: clipboard
        visible: false
    }
    Timer {
        id: copiedFor
        interval: 2500
        onTriggered: card.copied = ""
    }

    // How it stands, in colour: plain words, since a pill would read as a
    // control.
    Flow {
        visible: card.stage === "done"
        width: card.innerWidth
        spacing: 16
        Repeater {
            model: [{ n: card.bad, words: card.bad === 1 ? "problem" : "problems", color: theme.bad },
                    { n: card.warn, words: "to look at", color: theme.warn },
                    { n: card.good, words: "good", color: theme.good }].filter(c => c.n > 0)
            Item {
                height: 20
                width: tally.implicitWidth
                Row {
                    id: tally
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: modelData.color
                    }
                    Text {
                        text: modelData.n + " " + modelData.words
                        color: theme.quiet
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                }
            }
        }
    }

    Column {
        visible: card.stage === "looking"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: 4
            Rectangle {
                width: card.innerWidth
                height: 56; radius: theme.cardRadius
                color: theme.inset
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.5; to: 1; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1; to: 0.5; duration: 700; easing.type: Easing.InOutSine }
                }
            }
        }
    }

    Column {
        objectName: "healthItems"
        visible: card.stage === "done"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: card.stage === "done" ? card.items : []
            CardRow {
                objectName: "health-" + modelData.id
                // A finding no card can fix is copied, to ask someone about.
                readonly property bool asks: modelData.action === "ask" && modelData.question !== ""
                readonly property bool canAct: modelData.action !== "" && modelData.action !== "ask"
                standing: modelData.state
                tone: modelData.state === "bad" ? theme.bad : modelData.state === "warn" ? theme.warn : theme.good
                title: modelData.title
                detail: modelData.detail
                trailing: asks ? (card.copied === modelData.id ? "Copied" : "Copy for help") : canAct ? modelData.actionLabel : ""
                opens: canAct
                acts: asks
                onClicked: card.act(modelData)
            }
        }
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: card.stage === "done"
        Pill {
            implicitHeight: 44
            padding: 16
            fontSize: 15
            iconPath: Icons.refresh
            text: "Check again"
            onClicked: card.job.check()
        }
        Pill {
            objectName: "healthClose"
            implicitHeight: 44
            padding: 16
            fontSize: 15
            text: "Close"
            onClicked: card.job.close()
        }
    }
}
