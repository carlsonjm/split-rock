// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Update card: what is waiting, then the update itself with its
// progress, then what needs the person afterwards (src/app/SystemUpdate.h).
CardFrame {
    id: card
    objectName: "updateCard"
    readonly property var job: conversation.update
    readonly property string stage: card.job.stage
    readonly property var plan: card.job.plan
    readonly property var result: card.job.result
    readonly property bool blocked: (plan.blockers || []).length > 0
    hue: card.stage === "failed" ? "coral" : card.stage === "done" ? "green" : "blue"

    // The heading: what the card is, and where it has got to.
    CardHead {
        headingObjectName: "updateHeading"
        tone: card.tone
        icon: card.stage === "done" ? Icons.check : card.stage === "failed" ? Icons.alert : Icons.refresh
        spinning: card.stage === "looking" || card.stage === "updating"
        heading: {
            switch (card.stage) {
            case "looking": return "Looking for updates"
            case "nothing": return "Everything is up to date"
            case "updating": return "Updating this computer"
            case "done": return "This computer is up to date"
            case "failed": return "The update didn't finish"
            }
            if (card.plan.system < 0)
                return "Update this computer"
            const count = card.plan.system + card.plan.apps
            return count === 1 ? "1 update is waiting" : count + " updates are waiting"
        }
        sub: {
            switch (card.stage) {
            case "looking": return "Checking what's new, the news from Arch, and that it's a good moment."
            case "nothing": return card.plan.community > 0
                                   ? "Your community apps have " + card.plan.community + " updates; open Community apps to look at each one."
                                   : "Nothing needs doing."
            case "updating": return "You can keep working. The computer stays awake until it's done."
            case "done": return card.result.installed > 0 ? card.result.installed + " packages were updated." : ""
            case "failed": return card.result.why
            }
            const parts = []
            if (card.plan.system > 0)
                parts.push(card.plan.system + " for the system")
            if (card.plan.apps > 0)
                parts.push(card.plan.apps + (card.plan.apps === 1 ? " app" : " apps"))
            return parts.join(" and ")
        }
    }

    // Looking: three soft bars where the plan will be.
    Column {
        visible: card.stage === "looking"
        width: card.innerWidth
        spacing: 10
        Repeater {
            model: [0.9, 0.7, 0.45]
            Rectangle {
                width: card.innerWidth * modelData
                height: 12; radius: 6
                color: theme.chip
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.5; to: 1; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 1; to: 0.5; duration: 700; easing.type: Easing.InOutSine }
                }
            }
        }
    }

    // Ready: what the person will notice, then what to read first.
    Flow {
        visible: card.stage === "ready" && (card.plan.highlights || []).length > 0
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: card.plan.highlights || []
            Rectangle {
                height: 34
                width: chipText.implicitWidth + 28
                radius: 17
                color: theme.soft(card.tone)
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: modelData
                    color: theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    Column {
        visible: card.stage === "ready"
                 && (card.plan.blockers || []).length + (card.plan.warnings || []).length + (card.plan.news || []).length > 0
        width: card.innerWidth
        spacing: 10
        Repeater {
            model: card.stage === "ready" ? card.plan.blockers : []
            CardNote { text: modelData; tone: theme.bad }
        }
        Repeater {
            model: card.stage === "ready" ? card.plan.warnings : []
            CardNote { text: modelData }
        }
        Repeater {
            model: card.stage === "ready" ? card.plan.news : []
            CardNote {
                text: modelData.date + ": " + modelData.title
                tone: theme.accent
                link: modelData.link
                onLinkOpened: url => card.job.openLink(url)
            }
        }
    }

    Flow {
        visible: card.stage === "ready"
        width: card.innerWidth
        spacing: 18
        CardFact {
            icon: Icons.shield
            label: card.plan.snapshots ? "Restore point first" : "No restore point"
            tone: card.plan.snapshots ? theme.good : theme.warn
        }
        CardFact {
            visible: card.plan.system !== 0
            icon: Icons.lock
            label: "Needs your password"
        }
        CardFact {
            visible: card.plan.restartLikely
            icon: Icons.power
            label: "Restart after"
        }
    }

    // Updating: the bar, and what it is doing now.
    CardProgress {
        visible: card.stage === "updating"
        barObjectName: "updateProgress"
        tone: card.tone
        done: card.job.done
        total: card.job.total
        now: card.job.now
    }

    // Done: what needs the person now.
    Column {
        visible: card.stage === "done" || card.stage === "failed"
        width: card.innerWidth
        spacing: 10
        CardNote {
            visible: card.stage === "done" && card.result.restart
            tone: theme.accent
            text: "Restart to finish: the new parts start with the computer."
        }
        CardNote {
            visible: card.stage === "done" && (card.result.settingsFiles || []).length > 0
            text: {
                const n = (card.result.settingsFiles || []).length
                return (n === 1 ? "1 settings file has" : n + " settings files have")
                    + " a new version beside yours. Yours were kept; ask " + productName + " to compare them."
            }
        }
        CardNote {
            visible: card.stage === "done" && (card.result.failedServices || []).length > 0
            tone: theme.bad
            text: "Not running since: " + (card.result.failedServices || []).join(", ") + ". Ask " + productName + " to look into it."
        }
        CardNote {
            visible: card.result.restorePoint >= 0
            tone: theme.good
            text: "Restore point " + card.result.restorePoint + " was taken just before, if anything seems wrong."
        }
    }

    Fold {
        width: card.innerWidth
        title: card.stage === "updating" || card.stage === "done" || card.stage === "failed" ? "Show what it did" : "Show every package"
        body: card.stage === "ready" ? (card.plan.packages || "")
            : card.stage === "updating" || card.stage === "done" || card.stage === "failed" ? card.job.log : ""
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: card.stage !== "looking" && card.stage !== "updating"
        Pill {
            objectName: "updateButton"
            visible: card.stage === "ready"
            implicitHeight: 52
            padding: 30
            fontSize: 16
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            enabled: !card.blocked
            iconPath: Icons.refresh
            text: "Update"
            onClicked: card.job.start()
        }
        Pill {
            objectName: "restartButton"
            visible: card.stage === "done" && card.result.restart
            implicitHeight: 52
            padding: 26
            fontSize: 16
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            iconPath: Icons.power
            text: "Restart now"
            onClicked: card.job.restart()
        }
        Pill {
            visible: card.stage === "failed"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: "Try again"
            onClicked: card.job.check()
        }
        Pill {
            objectName: "updateClose"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: card.stage === "ready" ? "Not now" : card.stage === "done" && card.result.restart ? "Later" : "Close"
            onClicked: card.job.close()
        }
    }
}
