// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Free up space card: what takes room the person doesn't need, each
// ticked or not, on a bar of the disk; then one tap clears what is ticked
// (src/app/SpaceCleanup.h).
CardFrame {
    id: card
    objectName: "spaceCard"
    readonly property var job: helpers.space
    readonly property string stage: card.job.stage
    readonly property var items: card.job.items
    readonly property bool anyChosen: card.items.some(i => i.chosen)
    hue: card.stage === "failed" ? "coral" : "teal"

    // "1.2 GB", "340 MB".
    function size(kb) {
        if (kb >= 1024 * 1024)
            return (kb / 1024 / 1024).toFixed(kb >= 10 * 1024 * 1024 ? 0 : 1) + " GB"
        return Math.round(kb / 1024) + " MB"
    }

    CardHead {
        headingObjectName: "spaceHeading"
        tone: card.tone
        icon: card.stage === "done" ? Icons.check : card.stage === "failed" ? Icons.alert : Icons.drive
        spinning: card.stage === "looking"
        heading: {
            switch (card.stage) {
            case "looking": return "Looking for space to free up"
            case "nothing": return "Nothing needs clearing"
            case "clearing": return "Freeing up space"
            case "done": return "Freed up " + card.job.freed
            case "failed": return "Freeing up space didn't finish"
            }
            return card.job.chosenKb > 0 ? "Up to " + card.job.chosenSize + " can come free" : "Pick what to clear"
        }
        sub: {
            switch (card.stage) {
            case "looking": return "Old copies, logs, crash reports and leftovers. Nothing changes until you say."
            case "nothing": return "There are no leftovers worth clearing."
            case "clearing": return "You can keep working."
            case "failed": return card.job.why
            case "done":
                if (card.job.heldBack !== "")
                    return card.job.heldBack
                break
            }
            return card.job.diskKb > 0 ? card.size(card.job.freeKb) + " free of " + card.size(card.job.diskKb) + "." : ""
        }
    }

    // The disk: what is used, what the ticked rows free, and what is free.
    Item {
        objectName: "spaceBar"
        visible: (card.stage === "ready" || card.stage === "done") && card.job.diskKb > 0
        width: card.innerWidth
        height: 14
        Rectangle {
            id: track
            anchors.fill: parent
            radius: 7
            color: theme.soft(card.tone, 0.08)
            border.width: 1
            border.color: theme.hairline
            clip: true
            Rectangle {
                id: used
                height: parent.height
                width: parent.width * Math.max(0, Math.min(1, 1 - card.job.freeKb / card.job.diskKb))
                radius: 7
                color: theme.chip
                Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
            }
            // The ticked ones, at the end of what is used, each in its colour.
            Row {
                anchors.right: used.right
                height: parent.height
                visible: card.stage === "ready"
                Repeater {
                    model: card.items.filter(i => i.chosen && i.kb > 0)
                    Rectangle {
                        height: track.height
                        width: Math.max(3, track.width * modelData.kb / card.job.diskKb)
                        color: theme.hue(modelData.tone)
                        Behavior on width { NumberAnimation { duration: 200 } }
                    }
                }
            }
        }
    }

    // Looking: soft bars where the list will be.
    Column {
        visible: card.stage === "looking"
        width: card.innerWidth
        spacing: 10
        Repeater {
            model: [0.9, 0.7, 0.8, 0.5]
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

    Column {
        objectName: "spaceItems"
        visible: card.stage === "ready"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: card.stage === "ready" ? card.items : []
            CardRow {
                objectName: "space-" + modelData.id
                checkable: true
                checked: modelData.chosen
                tone: theme.hue(modelData.tone)
                title: modelData.title
                detail: modelData.detail
                trailing: modelData.size
                list: modelData.list
                listTitle: "Show which"
                onClicked: card.job.choose(modelData.id, !modelData.chosen)
            }
        }
    }

    // The apps the person added, at the end and never ticked: freeing up
    // space never removes an app. A tap opens Your apps.
    CardRow {
        objectName: "spaceYours"
        visible: (card.stage === "ready" || card.stage === "nothing") && card.job.yoursKb > 0
        width: card.innerWidth
        icon: Icons.apps
        title: "Your apps"
        detail: "Apps you added take " + card.job.yoursSize + ". See which, biggest first."
        trailing: card.job.yoursSize
        opens: true
        onClicked: helpers.openChore("apps")
    }

    // Downloads and the Desktop, last and never ticked: the person's own
    // files go only from Tidy up Downloads, to the Trash, file by file.
    CardRow {
        objectName: "spaceTidy"
        visible: card.stage === "ready" || card.stage === "nothing"
        width: card.innerWidth
        icon: Icons.tidy
        title: "Tidy up Downloads"
        detail: "Old downloads and copies, to the Trash"
        opens: true
        onClicked: helpers.openChore("tidy")
    }

    // Parts nothing uses, once ticked: every package that will be removed,
    // by name, before the button is tapped. Nothing else goes with them.
    Column {
        id: removal
        readonly property var orphans: card.items.find(i => i.id === "orphans" && i.chosen)
        readonly property var names: orphans ? orphans.list.split("\n").filter(n => n !== "") : []
        visible: card.stage === "ready" && removal.names.length > 0
        width: card.innerWidth
        spacing: 6
        Text {
            text: removal.names.length === 1 ? "This package will be removed" : "These " + removal.names.length + " packages will be removed"
            color: theme.quiet
            font.pixelSize: 13
            font.weight: Font.Medium
        }
        Rectangle {
            objectName: "spaceRemoveList"
            width: card.innerWidth
            height: Math.min(removeColumn.implicitHeight, 220) + 16
            radius: theme.cardRadius
            color: theme.sheet
            border.width: 1
            border.color: theme.line
            Flickable {
                id: removeScroller
                x: 14; y: 8
                width: parent.width - 28
                height: parent.height - 16
                contentWidth: width
                contentHeight: removeColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height
                WheelScroll { flickable: removeScroller }
                Column {
                    id: removeColumn
                    width: removeScroller.width
                    Repeater {
                        model: removal.names
                        Text {
                            width: removeColumn.width
                            height: 26
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: modelData
                            color: theme.text
                            font.family: theme.mono
                            font.pixelSize: 13
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
            visible: card.job.needsPassword
            icon: Icons.lock
            label: "Needs your password"
        }
        CardFact {
            visible: card.items.some(i => i.id === "orphans" && i.chosen)
            icon: Icons.shield
            label: "Restore point first"
            tone: theme.good
        }
    }

    CardProgress {
        visible: card.stage === "clearing"
        barObjectName: "spaceProgress"
        tone: card.tone
        done: card.job.done
        total: card.job.total
        now: card.job.now
    }

    CardNote {
        visible: card.stage === "done"
        tone: theme.good
        text: card.size(card.job.freeKb) + " is free now."
    }

    Fold {
        width: card.innerWidth
        title: "Show what it did"
        body: card.stage === "done" || card.stage === "failed" ? card.job.log : ""
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: !card.job.busy
        Pill {
            objectName: "spaceButton"
            visible: card.stage === "ready"
            implicitHeight: 44
            padding: 30
            fontSize: 16
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            enabled: card.anyChosen
            iconPath: Icons.drive
            text: card.job.chosenKb > 0 ? "Free up " + card.job.chosenSize : "Free up space"
            onClicked: card.job.start()
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
            objectName: "spaceClose"
            implicitHeight: 44
            padding: 16
            fontSize: 15
            text: card.stage === "ready" ? "Not now" : "Close"
            onClicked: card.job.close()
        }
    }
}
