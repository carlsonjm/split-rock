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

    // The row whose way back is being asked about, before it is done.
    property var confirming: null
    readonly property bool asking: card.confirming !== null && card.stage === "ready"

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
        case "putback":
        case "start":
        case "goback":
            card.confirming = item
            break
        case "settings":
            card.job.openSettings(item.page)
            break
        case "ask":
            // Described as the Health check describes a problem: what changed,
            // exactly, then what this computer is.
            clipboard.text = helpers.health.helpText(item.help)
            clipboard.selectAll()
            clipboard.copy()
            card.copied = item.id
            copiedFor.restart()
            break
        }
    }

    function doIt() {
        const item = card.confirming
        card.confirming = null
        if (item.action === "putback")
            helpers.putBack(item.id)
        else if (item.action === "start")
            card.job.startAgain(item.id)
        else if (item.action === "goback")
            card.job.goBack(item.id)
    }

    onStageChanged: if (card.stage !== "ready") card.confirming = null

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
        icon: card.asking ? card.iconFor(card.confirming.kind) : Icons.undo
        spinning: card.stage === "looking" || card.stage === "working"
        heading: card.asking ? card.confirming.confirmTitle
               : card.stage === "looking" ? "Looking at what changed"
               : card.stage === "working" ? card.job.now
               : card.stage === "nothing" ? "Nothing changed lately"
               : card.latest.title !== undefined ? "Your last change: " + card.latest.title
               : "What changed lately"
        sub: card.asking ? card.confirming.confirmSub
           : card.stage === "looking" ? "The last two weeks. It only reads; nothing changes."
           : card.stage === "working" ? "This takes a moment. What changed looks again when it's done."
           : card.stage === "nothing" ? "No updates, installs, settings or stopped services in the last two weeks."
           : card.latest.title !== undefined ? card.latest.day + ". Undo puts it back as it was; below is everything else from the last two weeks."
           : "The last two weeks, newest first. Each has its way back."
    }

    CardNote {
        objectName: "changesNotice"
        visible: card.job.notice !== "" && !card.asking && card.stage !== "looking" && card.stage !== "working"
        tone: card.tone
        text: card.job.notice
    }

    // Before a way back is taken: what it does, exactly, and the choice.
    Column {
        objectName: "changesConfirm"
        visible: card.asking
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: card.asking ? card.confirming.confirmLines : []
            // "What\nwhat it means", as a row's title and its line under it.
            CardRow {
                width: card.innerWidth
                title: modelData.split("\n")[0]
                detail: modelData.split("\n").slice(1).join(" ")
            }
        }
        Fold {
            width: card.innerWidth
            body: card.asking ? card.confirming.exact.join("\n") : ""
        }
    }
    Flow {
        visible: card.asking
        width: card.innerWidth
        spacing: 8
        Pill {
            objectName: "changesDoIt"
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            enabled: !helpers.changing && !helpers.choreBusy
            text: "Do it"
            onClicked: card.doIt()
        }
        Pill {
            objectName: "changesShowMe"
            visible: card.asking && card.confirming.page !== ""
            text: "Show me how instead"
            onClicked: {
                card.job.openSettings(card.confirming.page)
                card.confirming = null
            }
        }
        Pill {
            objectName: "changesNotNow"
            text: "Not now"
            onClicked: card.confirming = null
        }
    }

    CardProgress {
        visible: card.stage === "working"
        barObjectName: "changesProgress"
        tone: card.tone
        total: 0
        now: card.job.now
    }

    Pill {
        objectName: "changesUndoLatest"
        visible: card.stage === "ready" && !card.asking && card.latest.title !== undefined
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
        visible: card.stage === "ready" && !card.asking
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
                    readonly property bool asks: modelData.action === "ask" && modelData.help !== ""
                    readonly property bool opensOther: modelData.action === "restore"
                    readonly property bool canAct: modelData.action !== "" && modelData.action !== "ask"
                                                    && (modelData.action !== "undo" || !helpers.changing)
                    tone: modelData.kind === "service" || modelData.kind === "restart" ? theme.warn : card.tone
                    icon: card.iconFor(modelData.kind)
                    title: modelData.title
                    detail: modelData.detail
                    trailing: opensOther && canAct ? modelData.actionLabel : ""
                    opens: opensOther && canAct
                    button: asks ? (card.copied === modelData.id ? "Copied" : "Copy for help")
                          : canAct && !opensOther ? modelData.actionLabel : ""
                    buttonQuiet: asks
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
        visible: card.stage !== "looking" && card.stage !== "working" && !card.asking
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
