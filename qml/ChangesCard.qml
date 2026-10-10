// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The What changed card: the last two weeks on this computer, a day at a
// time, each change with the way back that fits it (src/app/RecentChanges.h).
CardFrame {
    id: card
    objectName: "changesCard"
    readonly property var job: conversation.recent
    readonly property string stage: card.job.stage
    readonly property var items: card.job.items
    readonly property var latest: card.job.latest
    property bool all: false
    readonly property var shown: card.all ? card.items : card.items.slice(0, 8)
    hue: "pink"

    function iconFor(kind) {
        return ({ own: Icons.undo, update: Icons.refresh, install: Icons.download, remove: Icons.trash,
                  apps: Icons.box, settings: Icons.sliders, service: Icons.alert, restart: Icons.power })[kind] || Icons.history
    }

    function act(item) {
        switch (item.action) {
        case "undo":
            conversation.undoChange(item.change)
            break
        case "restore":
            conversation.openChore("restore")
            break
        case "restart":
            card.job.restart()
            break
        case "ask":
            if (conversation.phase !== "ready")
                return
            card.job.close()
            conversation.ask(item.question)
            break
        }
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
        implicitHeight: 52
        padding: 28
        fontSize: 16
        filled: true
        tone: card.tone
        toneInk: theme.hueInk
        iconPath: Icons.undo
        enabled: !conversation.changing
        text: "Undo it"
        onClicked: conversation.undoChange(card.latest.change)
    }

    Column {
        visible: card.stage === "looking"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: 3
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
                    font.weight: Font.Bold
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.6
                }
                CardRow {
                    objectName: "change-" + modelData.id
                    readonly property bool canAct: modelData.action !== ""
                                                    && (modelData.action !== "ask" || conversation.phase === "ready")
                                                    && (modelData.action !== "undo" || !conversation.changing)
                    tone: modelData.kind === "service" || modelData.kind === "restart" ? theme.warn : card.tone
                    icon: card.iconFor(modelData.kind)
                    title: modelData.title
                    detail: modelData.detail
                    trailing: canAct ? modelData.actionLabel : ""
                    opens: canAct
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
            objectName: "changesAsk"
            visible: card.stage === "ready" && conversation.phase === "ready"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            filled: card.latest.title === undefined
            tone: card.tone
            toneInk: theme.hueInk
            iconPath: Icons.search
            text: "Ask what caused it"
            onClicked: {
                const question = card.job.question
                card.job.close()
                conversation.ask(question)
            }
        }
        Pill {
            implicitHeight: 52
            padding: 22
            fontSize: 15
            iconPath: Icons.refresh
            text: "Look again"
            onClicked: card.job.check()
        }
        Pill {
            objectName: "changesClose"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: "Close"
            onClicked: card.job.close()
        }
    }
}
