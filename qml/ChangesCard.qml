// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The What changed card: the last two weeks on this computer, a day at a
// time, each change with the way back that fits it (src/app/RecentChanges.h).
CardFrame {
    id: card
    objectName: "changesCard"
    readonly property var job: helpers.recent
    readonly property string stage: card.job.stage
    readonly property var items: card.job.items
    readonly property var latest: card.job.latest
    property bool all: false
    readonly property var shown: card.all ? card.items : card.items.slice(0, 8)
    hue: "pink"

    // The row whose description was copied last, to say so for a moment.
    property string copied: ""

    function iconFor(kind) {
        return ({ own: Icons.undo, update: Icons.refresh, install: Icons.download, remove: Icons.trash,
                  apps: Icons.box, settings: Icons.sliders, service: Icons.alert, restart: Icons.power })[kind] || Icons.history
    }

    function act(item) {
        switch (item.action) {
        case "undo":
            helpers.undoChange(item.change)
            break
        case "restore":
            helpers.openChore("restore")
            break
        case "restart":
            card.job.restart()
            break
        case "ask":
            // Described as the Health check describes a problem: the question,
            // then what this computer is.
            clipboard.text = helpers.health.helpText(item.question)
            clipboard.selectAll()
            clipboard.copy()
            card.copied = item.id
            copiedFor.restart()
            break
        }
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

    CardHead {
        headingObjectName: "changesHeading"
        tone: card.tone
        icon: Icons.undo
        spinning: card.stage === "looking"
        heading: card.stage === "looking" ? "Looking at what changed"
               : card.stage === "nothing" ? "Nothing changed lately"
               : card.latest.title !== undefined ? "Your last change: " + card.latest.title
               : "What changed lately"
        sub: card.stage === "looking" ? "The last two weeks. It only reads; nothing changes."
           : card.stage === "nothing" ? "No updates, installs, settings or stopped services in the last two weeks."
           : card.latest.title !== undefined ? card.latest.day + ". Undo puts it back as it was; below is everything else from the last two weeks."
           : "The last two weeks, newest first. Each has its way back."
    }

    Pill {
        objectName: "changesUndoLatest"
        visible: card.stage === "ready" && card.latest.title !== undefined
        implicitHeight: 44
        padding: 16
        fontSize: 16
        filled: true
        tone: card.tone
        toneInk: theme.hueInk
        iconPath: Icons.undo
        enabled: !helpers.changing
        text: "Undo it"
        onClicked: helpers.undoChange(card.latest.change)
    }

    Column {
        visible: card.stage === "looking"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: 3
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
        objectName: "changesItems"
        visible: card.stage === "ready"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: card.stage === "ready" ? card.shown : []
            Column {
                width: card.innerWidth
                spacing: 8
                // The day, above its first change.
                Text {
                    visible: index === 0 || card.shown[index - 1].day !== modelData.day
                    topPadding: index === 0 ? 0 : 8
                    text: modelData.day
                    color: theme.quiet
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
                CardRow {
                    objectName: "change-" + modelData.id
                    // A change with no way back is copied, to ask someone about.
                    readonly property bool asks: modelData.action === "ask" && modelData.question !== ""
                    readonly property bool canAct: modelData.action !== "" && modelData.action !== "ask"
                                                    && (modelData.action !== "undo" || !helpers.changing)
                    tone: modelData.kind === "service" || modelData.kind === "restart" ? theme.warn : card.tone
                    icon: card.iconFor(modelData.kind)
                    title: modelData.title
                    detail: modelData.detail
                    trailing: asks ? (card.copied === modelData.id ? "Copied" : "Copy for help") : canAct ? modelData.actionLabel : ""
                    opens: canAct
                    acts: asks
                    onClicked: card.act(modelData)
                }
            }
        }
        Pill {
            visible: !card.all && card.items.length > 8
            implicitHeight: 40
            text: "Show " + (card.items.length - 8) + " more"
            onClicked: card.all = true
        }
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: card.stage !== "looking"
        Pill {
            implicitHeight: 44
            padding: 16
            fontSize: 15
            iconPath: Icons.refresh
            text: "Look again"
            onClicked: card.job.check()
        }
        Pill {
            objectName: "changesClose"
            implicitHeight: 44
            padding: 16
            fontSize: 15
            text: "Close"
            onClicked: card.job.close()
        }
    }
}
