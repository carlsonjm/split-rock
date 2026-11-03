// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Restore points card: each restore point as what was about to happen,
// and the way back to one through the computer's own startup menu
// (src/app/RestorePoints.h).
CardFrame {
    id: card
    objectName: "restoreCard"
    readonly property var job: helpers.restore
    readonly property string stage: card.job.stage
    readonly property var points: card.job.points
    readonly property var chosen: card.job.chosen
    property bool all: false
    readonly property int kept: card.job.kept
    readonly property string keepName: card.job.keepTool.indexOf("btrfs-assistant") === 0 ? "Btrfs Assistant" : card.job.keepTool
    hue: "amber"

    function iconFor(kind) {
        return ({ update: Icons.refresh, install: Icons.download, remove: Icons.trash, timeline: Icons.history,
                  boot: Icons.power })[kind] || Icons.shield
    }

    CardHead {
        headingObjectName: "restoreHeading"
        tone: card.tone
        icon: Icons.history
        spinning: card.stage === "looking" || card.stage === "deleting"
        heading: {
            switch (card.stage) {
            case "looking": return "Looking at restore points"
            case "nothing": return card.job.configured ? "No restore points yet" : "This computer doesn't take restore points"
            case "denied": return "Seeing them needs your password"
            case "chosen": return "Going back to " + card.chosen.when.replace(/^(Today|Yesterday)/, w => w.toLowerCase())
            case "booted": return "You're running restore point " + card.job.bootedFrom
            case "confirmClear": return "Clear " + card.job.deletingCount + " older restore points?"
            case "confirmDelete": return "Delete this restore point?"
            case "deleting": return card.job.deletingCount === 1 ? "Deleting the restore point" : "Deleting " + card.job.deletingCount + " restore points"
            }
            return card.points.length === 1 ? "1 restore point" : card.points.length + " restore points"
        }
        sub: {
            switch (card.stage) {
            case "looking": return "Each is how the system was just before a change."
            case "nothing": return card.job.note !== "" ? card.job.note : card.job.configured ? "One is taken before each update and install."
                                                       : "This computer isn't set up to take them."
            case "denied": return card.job.notShown !== "" ? card.job.notShown : "It only reads the list; nothing changes."
            case "chosen": return (card.job.note !== "" ? card.job.note + " " : "") + card.chosen.what + "."
                                  + (card.job.homeSeparate ? " Your own files stay as they are now." : "")
            case "booted": return "Nothing is kept yet. Restarting normally comes back to how it was."
            case "confirmClear": return "The newest " + card.kept + " stay, so you can still go back to before the last change. "
                                        + "You can't go back to the cleared ones afterwards."
            case "confirmDelete": return card.chosen.what + ", " + card.chosen.when.replace(/^(Today|Yesterday)/, w => w.toLowerCase())
                                         + ". You can't go back to it afterwards."
            case "deleting": return "The space comes back as it goes. You can keep working."
            }
            // Whose they are, and that nobody needs to tidy them away.
            const own = card.job.ownCount
            const whose = own === card.points.length ? productName + " took each one before a change it made."
                        : own === 0 ? "The system takes one by itself before every install, removal and update."
                        : productName + " took " + own + ", before changes it made; the system takes the rest by itself before every install, removal and update."
            return (card.job.note !== "" ? card.job.note + " " : "")
                   + "Each is how the system was just before a change. Tap one to go back to it. " + whose
                   + (card.job.keeping !== "" ? " " + card.job.keeping : "")
                   + (card.job.olderCount > 0 ? " To make room now, clear the older ones." : "")
        }
    }

    Column {
        objectName: "restorePoints"
        visible: card.stage === "ready"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: card.stage === "ready" ? (card.all ? card.points : card.points.slice(0, 6)) : []
            CardRow {
                objectName: "restore-" + modelData.number
                tone: card.tone
                icon: card.iconFor(modelData.kind)
                title: modelData.what
                detail: modelData.when + " · " + (modelData.own ? productName : "Automatic") + " · number " + modelData.number
                opens: true
                onClicked: card.job.choose(modelData.number)
            }
        }
        Pill {
            visible: !card.all && card.points.length > 6
            implicitHeight: 40
            text: "Show " + (card.points.length - 6) + " more"
            onClicked: card.all = true
        }
    }

    // The way back, step by step.
    Column {
        visible: card.stage === "chosen" && card.job.steps.length > 0
        width: card.innerWidth
        spacing: 10
        Repeater {
            model: card.stage === "chosen" ? card.job.steps : []
            Row {
                width: card.innerWidth
                spacing: 12
                Rectangle {
                    y: 1
                    width: 26; height: 26; radius: 13
                    color: theme.soft(card.tone, 0.22)
                    Text {
                        anchors.centerIn: parent
                        text: index + 1
                        color: card.tone
                        font.pixelSize: 13
                        font.weight: Font.Medium
                    }
                }
                Text {
                    width: parent.width - 38
                    wrapMode: Text.WordWrap
                    text: modelData
                    color: theme.text
                    font.pixelSize: 16
                    lineHeight: 1.4
                }
            }
        }
    }

    CardNote {
        visible: card.stage === "chosen" && card.job.steps.length === 0
        tone: theme.warn
        text: "This computer's startup menu doesn't list restore points, so there's no safe way back from here."
              + (card.job.keepTool !== "" ? " " + card.keepName + " can put one back." : "")
    }

    CardNote {
        visible: card.stage === "booted"
        tone: card.tone
        text: card.job.keepTool !== "" ? "If everything is right, keep it with " + card.keepName + ", the computer's own tool for it."
                                       : "This computer has no tool to keep it."
    }

    CardFact {
        visible: (card.stage === "ready" && card.job.olderCount > 0) || card.stage === "confirmClear"
                 || card.stage === "confirmDelete"
        icon: Icons.lock
        label: card.stage === "confirmDelete" ? "Needs your password"
                                              : "Clearing needs your password. The newest " + card.kept + " always stay."
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: card.stage !== "looking" && card.stage !== "deleting"
        Pill {
            objectName: "restoreShow"
            visible: card.stage === "denied"
            implicitHeight: 44
            padding: 16
            fontSize: 16
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            iconPath: Icons.lock
            text: "Show them"
            onClicked: card.job.checkAsAdmin()
        }
        Pill {
            objectName: "restoreRestart"
            visible: card.stage === "chosen" && card.job.steps.length > 0
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
            visible: (card.stage === "booted" || (card.stage === "chosen" && card.job.steps.length === 0)) && card.job.keepTool !== ""
            implicitHeight: 44
            padding: 16
            fontSize: 16
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            iconPath: Icons.external
            text: card.stage === "booted" ? "Keep this" : "Open " + card.keepName
            onClicked: card.job.openKeepTool()
        }
        Pill {
            visible: card.stage === "booted"
            implicitHeight: 44
            padding: 16
            fontSize: 15
            iconPath: Icons.power
            text: "Restart normally"
            onClicked: card.job.restart()
        }
        Pill {
            visible: card.stage === "chosen"
            implicitHeight: 44
            padding: 16
            fontSize: 15
            iconPath: Icons.back
            text: "Back"
            onClicked: card.job.back()
        }
        Pill {
            objectName: "restoreClearOlder"
            visible: card.stage === "ready" && card.job.olderCount > 0
            implicitHeight: 44
            padding: 16
            fontSize: 15
            iconPath: Icons.trash
            text: "Clear " + card.job.olderCount + " older ones"
            onClicked: card.job.askClearOlder()
        }
        Pill {
            objectName: "restoreDelete"
            visible: card.stage === "chosen" && card.chosen.number !== card.job.bootedFrom
            implicitHeight: 44
            padding: 16
            fontSize: 15
            iconPath: Icons.trash
            text: "Delete"
            onClicked: card.job.askDelete()
        }
        Pill {
            objectName: "restoreConfirmDelete"
            visible: card.stage === "confirmClear" || card.stage === "confirmDelete"
            implicitHeight: 44
            padding: 16
            fontSize: 16
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            iconPath: Icons.trash
            text: card.stage === "confirmClear" ? "Clear them" : "Delete it"
            onClicked: card.job.confirmDelete()
        }
        Pill {
            objectName: "restoreCancelDelete"
            visible: card.stage === "confirmClear" || card.stage === "confirmDelete"
            implicitHeight: 44
            padding: 16
            fontSize: 15
            text: "Cancel"
            onClicked: card.job.cancelDelete()
        }
        Pill {
            objectName: "restoreClose"
            visible: card.stage !== "confirmClear" && card.stage !== "confirmDelete"
            implicitHeight: 44
            padding: 16
            fontSize: 15
            text: "Close"
            onClicked: card.job.close()
        }
    }
}
