// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Update card: what is waiting, then the update itself with its
// progress, then what needs the person afterwards (src/app/SystemUpdate.h).
CardFrame {
    id: card
    objectName: "updateCard"
    readonly property var job: helpers.update
    readonly property string stage: card.job.stage
    readonly property var plan: card.job.plan
    readonly property var result: card.job.result
    readonly property bool blocked: (plan.blockers || []).length > 0
    // The list is on the card while the person decides.
    readonly property bool reading: card.stage === "ready" || card.stage === "confirming"
    hue: card.stage === "failed" ? "coral" : card.stage === "done" ? "green" : "blue"

    // The heading: what the card is, and where it has got to.
    CardHead {
        headingObjectName: "updateHeading"
        tone: card.tone
        icon: card.stage === "done" ? Icons.check : card.stage === "failed" ? Icons.alert : Icons.refresh
        spinning: card.stage === "looking" || card.stage === "rechecking" || card.stage === "updating"
        heading: {
            switch (card.stage) {
            case "looking": return "Looking for updates"
            case "rechecking": return "Checking the list once more"
            case "confirming": return "Shuffle may need its own update first"
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
            case "rechecking": return "Making sure nothing new arrived since you read it."
            case "confirming": return "Read the note below. Updating anyway is your call."
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
            const sub = parts.join(" and ")
            return card.plan.download ? sub + " · " + card.plan.download + " to download" : sub
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

    // Ready: the list changed under the person, then the big pieces of the
    // desktop the update moves, then what to read first.
    CardNote {
        objectName: "updateChanged"
        visible: card.reading && card.plan.changed === true
        width: card.innerWidth
        tone: theme.warn
        text: "The list changed since you looked. Read it again before updating."
    }

    Column {
        objectName: "updatePieces"
        visible: card.reading && (card.plan.pieces || []).length > 0
        width: card.innerWidth
        spacing: 6
        Text {
            text: "Big pieces in this update"
            color: theme.quiet
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Repeater {
            model: card.reading ? card.plan.pieces || [] : []
            Rectangle {
                width: card.innerWidth
                height: 48
                radius: theme.cardRadius
                color: modelData.step ? theme.soft(theme.warn, 0.16) : theme.inset
                Text {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - x - pieceVersion.implicitWidth - 28
                    elide: Text.ElideRight
                    text: modelData.piece
                    color: theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Text {
                    id: pieceVersion
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: (modelData.from ? modelData.from + " → " : "New ") + modelData.to
                          + (modelData.step ? " · new version" : "")
                    color: modelData.step ? theme.warn : theme.quiet
                    font.pixelSize: 14
                    font.weight: modelData.step ? Font.Bold : Font.Normal
                }
            }
        }
    }

    Column {
        visible: card.reading
                 && (card.plan.blockers || []).length + (card.plan.warnings || []).length + (card.plan.news || []).length > 0
        width: card.innerWidth
        spacing: 10
        Repeater {
            model: card.reading ? card.plan.blockers : []
            CardNote { text: modelData; tone: theme.bad }
        }
        Repeater {
            model: card.reading ? card.plan.warnings : []
            CardNote { text: modelData; tone: card.stage === "confirming" ? theme.bad : theme.warn }
        }
        Repeater {
            model: card.reading ? card.plan.news : []
            CardNote {
                text: modelData.date + ": " + modelData.title
                tone: theme.accent
                link: modelData.link
                onLinkOpened: url => card.job.openLink(url)
            }
        }
    }

    // Everything the update installs, in a box of its own so a long list
    // scrolls and the buttons stay in reach.
    Column {
        visible: card.reading && ((card.plan.list || []).length + (card.plan.appIds || []).length) > 0
        width: card.innerWidth
        spacing: 6
        Text {
            text: "Everything this installs"
            color: theme.quiet
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Rectangle {
            objectName: "updateList"
            width: card.innerWidth
            height: Math.min(listColumn.implicitHeight, 300) + 16
            radius: theme.cardRadius
            color: theme.sheet
            border.width: 1
            border.color: theme.line
            Flickable {
                id: listScroller
                x: 14; y: 8
                width: parent.width - 28
                height: parent.height - 16
                contentWidth: width
                contentHeight: listColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                WheelScroll { flickable: listScroller }
                Column {
                    id: listColumn
                    width: listScroller.width
                    Repeater {
                        model: card.reading ? (card.plan.list || []).concat((card.plan.appIds || []).map(id => ({ name: id, app: true }))) : []
                        Item {
                            width: listColumn.width
                            height: 28
                            Text {
                                id: packageName
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, parent.width * 0.5)
                                elide: Text.ElideRight
                                text: modelData.name
                                color: theme.text
                                font.family: theme.mono
                                font.pixelSize: 13
                            }
                            Text {
                                anchors.left: packageName.right
                                anchors.leftMargin: 10
                                anchors.right: packageSize.left
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                text: modelData.app ? "app" : (modelData.from ? modelData.from + " → " : "new · ") + modelData.to
                                color: modelData.step ? theme.warn : theme.quiet
                                font.family: theme.mono
                                font.pixelSize: 13
                            }
                            Text {
                                id: packageSize
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.size || ""
                                color: theme.quiet
                                font.pixelSize: 13
                            }
                        }
                    }
                }
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
            objectName: "updateCarryOn"
            visible: card.stage === "done" && !card.result.restart
            tone: theme.good
            text: "Nothing needs a restart: it's safe to carry on as you are."
        }
        CardNote {
            visible: card.stage === "done" && (card.result.settingsFiles || []).length > 0
            text: {
                const n = (card.result.settingsFiles || []).length
                return (n === 1 ? "1 settings file has" : n + " settings files have")
                    + " a new version beside yours. Yours were kept."
            }
        }
        CardNote {
            visible: card.stage === "done" && (card.result.failedServices || []).length > 0
            tone: theme.bad
            text: "Not running since: " + (card.result.failedServices || []).join(", ") + ". Health check shows what stopped."
        }
        CardNote {
            visible: card.result.restorePoint >= 0
            tone: theme.good
            text: "Restore point " + card.result.restorePoint + " was taken just before, if anything seems wrong."
        }
    }

    Fold {
        width: card.innerWidth
        title: "Show what it did"
        body: card.stage === "updating" || card.stage === "done" || card.stage === "failed" ? card.job.log : ""
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: card.stage !== "looking" && card.stage !== "rechecking" && card.stage !== "updating"
        Pill {
            objectName: "updateButton"
            visible: card.stage === "ready"
            implicitHeight: 44
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
            objectName: "updateAnyway"
            visible: card.stage === "confirming"
            implicitHeight: 44
            padding: 30
            fontSize: 16
            filled: true
            tone: theme.bad
            toneInk: theme.hueInk
            iconPath: Icons.refresh
            text: "Update anyway"
            onClicked: card.job.confirm()
        }
        Pill {
            objectName: "restartButton"
            visible: card.stage === "done" && card.result.restart
            implicitHeight: 44
            padding: 16
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
            implicitHeight: 44
            padding: 16
            fontSize: 15
            text: "Try again"
            onClicked: card.job.check()
        }
        Pill {
            objectName: "updateClose"
            implicitHeight: 44
            padding: 16
            fontSize: 15
            text: card.reading ? "Not now" : card.stage === "done" && card.result.restart ? "Later" : "Close"
            onClicked: card.job.close()
        }
    }
}
