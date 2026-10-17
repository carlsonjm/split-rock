// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Health check card: each part of the computer as good, worth a look or a
// problem, with the card or the question that helps (src/app/HealthCheck.h).
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

    function act(item) {
        if (item.action === "restart") {
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
           : "Checked " + card.job.lastChecked.toLowerCase() + ". It runs by itself once a week."
    }

    // How it stands, in colour.
    Flow {
        visible: card.stage === "done"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: [{ n: card.bad, words: card.bad === 1 ? "problem" : "problems", color: theme.bad },
                    { n: card.warn, words: "to look at", color: theme.warn },
                    { n: card.good, words: "good", color: theme.good }].filter(c => c.n > 0)
            Rectangle {
                height: 34
                width: tally.implicitWidth + 30
                radius: 17
                color: theme.soft(modelData.color, 0.16)
                Row {
                    id: tally
                    anchors.centerIn: parent
                    spacing: 8
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8; height: 8; radius: 4
                        color: modelData.color
                    }
                    Text {
                        text: modelData.n + " " + modelData.words
                        color: theme.text
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
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
                height: 64; radius: 16
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
                // Asking about a finding was the old chat's.
                readonly property bool canAct: modelData.action !== "" && modelData.action !== "ask"
                standing: modelData.state
                tone: modelData.state === "bad" ? theme.bad : modelData.state === "warn" ? theme.warn : theme.good
                title: modelData.title
                detail: modelData.detail
                trailing: canAct ? modelData.actionLabel : ""
                opens: canAct
                onClicked: card.act(modelData)
            }
        }
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: card.stage === "done"
        Pill {
            implicitHeight: 52
            padding: 22
            fontSize: 15
            iconPath: Icons.refresh
            text: "Check again"
            onClicked: card.job.check()
        }
        Pill {
            objectName: "healthClose"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: "Close"
            onClicked: card.job.close()
        }
    }
}
