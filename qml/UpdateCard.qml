// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Update card: what is waiting, then the update itself with its
// progress, then what needs the person afterwards (src/app/SystemUpdate.h).
Rectangle {
    id: card
    objectName: "updateCard"
    readonly property var job: conversation.update
    readonly property string stage: card.job.stage
    readonly property var plan: card.job.plan
    readonly property var result: card.job.result
    readonly property bool blocked: (plan.blockers || []).length > 0

    implicitHeight: body.implicitHeight + 48
    radius: theme.cardRadius
    color: theme.raised
    border.width: 1
    border.color: theme.hairline

    // A fact under the heading: an icon and a few words.
    component Fact: Row {
        property string icon
        property string label
        property color tone: theme.quiet
        spacing: 7
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            width: 15; height: 15
            path: parent.icon
            color: parent.tone
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: parent.label
            color: theme.quiet
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }
    }

    // A line that needs reading: a warning, a reason it can't start, or a
    // notice from Arch with a link to it.
    component Note: Row {
        property string text
        property color tone: theme.warn
        property string link
        width: body.width
        spacing: 12
        Rectangle {
            y: 7
            width: 8; height: 8; radius: 4
            color: parent.tone
        }
        Text {
            width: parent.width - 20
            wrapMode: Text.WordWrap
            text: parent.link !== "" ? parent.text + "  <a href=\"" + parent.link + "\">Read it</a>" : parent.text
            textFormat: parent.link !== "" ? Text.StyledText : Text.PlainText
            linkColor: theme.accent
            color: theme.text
            font.pixelSize: 15
            lineHeight: 1.35
            onLinkActivated: link => card.job.openLink(link)
        }
    }

    Column {
        id: body
        x: 24; y: 24
        width: parent.width - 48
        spacing: 18

        // The heading: what the card is, and where it has got to.
        Row {
            spacing: 14
            Rectangle {
                width: 44; height: 44; radius: 14
                color: card.stage === "failed" ? Qt.rgba(theme.bad.r, theme.bad.g, theme.bad.b, 0.16)
                     : card.stage === "done" ? Qt.rgba(theme.good.r, theme.good.g, theme.good.b, 0.16)
                     : theme.accentSoft
                Icon {
                    id: statusIcon
                    anchors.centerIn: parent
                    width: 22; height: 22
                    stroke: 2
                    path: card.stage === "done" ? Icons.check : card.stage === "failed" ? Icons.alert : Icons.refresh
                    color: card.stage === "failed" ? theme.bad : card.stage === "done" ? theme.good : theme.accent
                    RotationAnimation on rotation {
                        running: card.stage === "looking" || card.stage === "updating"
                        loops: Animation.Infinite
                        from: 0; to: 360
                        duration: 2400
                        // Upright again once it stops, for the tick or the
                        // warning that follows.
                        onRunningChanged: if (!running) statusIcon.rotation = 0
                    }
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: body.width - 58
                spacing: 3
                Text {
                    objectName: "updateHeading"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: {
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
                    color: theme.text
                    font.pixelSize: 22
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    visible: text !== ""
                    text: {
                        switch (card.stage) {
                        case "looking": return "Checking what's new, the news from Arch, and that it's a good moment."
                        case "nothing": return card.plan.community > 0
                                               ? "Your community apps have " + card.plan.community + " updates; Genie offers them one at a time."
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
                    color: theme.quiet
                    font.pixelSize: 15
                    lineHeight: 1.3
                }
            }
        }

        // Looking: three soft bars where the plan will be.
        Column {
            visible: card.stage === "looking"
            width: body.width
            spacing: 10
            Repeater {
                model: [0.9, 0.7, 0.45]
                Rectangle {
                    width: body.width * modelData
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
            width: body.width
            spacing: 8
            Repeater {
                model: card.plan.highlights || []
                Rectangle {
                    height: 34
                    width: chipText.implicitWidth + 28
                    radius: 17
                    color: theme.chip
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
            width: body.width
            spacing: 10
            Repeater {
                model: card.stage === "ready" ? card.plan.blockers : []
                Note { text: modelData; tone: theme.bad }
            }
            Repeater {
                model: card.stage === "ready" ? card.plan.warnings : []
                Note { text: modelData }
            }
            Repeater {
                model: card.stage === "ready" ? card.plan.news : []
                Note {
                    text: modelData.date + ": " + modelData.title
                    tone: theme.accent
                    link: modelData.link
                }
            }
        }

        Flow {
            visible: card.stage === "ready"
            width: body.width
            spacing: 18
            Fact {
                icon: Icons.shield
                label: card.plan.snapshots ? "Restore point first" : "No restore point"
                tone: card.plan.snapshots ? theme.good : theme.warn
            }
            Fact {
                visible: card.plan.system !== 0
                icon: Icons.lock
                label: "Needs your password"
            }
            Fact {
                visible: card.plan.restartLikely
                icon: Icons.power
                label: "Restart after"
            }
        }

        // Updating: the bar, and what it is doing now.
        Column {
            visible: card.stage === "updating"
            width: body.width
            spacing: 10
            Rectangle {
                objectName: "updateProgress"
                width: parent.width
                height: 8; radius: 4
                color: theme.chip
                Rectangle {
                    height: parent.height; radius: 4
                    color: theme.accent
                    width: card.job.total > 0 ? Math.max(height, parent.width * Math.min(1, card.job.done / card.job.total)) : height * 3
                    Behavior on width { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                }
            }
            Row {
                width: parent.width
                Text {
                    width: parent.width - countText.width
                    elide: Text.ElideRight
                    text: card.job.now
                    color: theme.text
                    font.pixelSize: 15
                }
                Text {
                    id: countText
                    visible: card.job.total > 0
                    text: card.job.done + " of " + card.job.total
                    color: theme.quiet
                    font.pixelSize: 14
                }
            }
        }

        // Done: what needs the person now.
        Column {
            visible: card.stage === "done" || card.stage === "failed"
            width: body.width
            spacing: 10
            Note {
                visible: card.stage === "done" && card.result.restart
                tone: theme.accent
                text: "Restart to finish: the new parts start with the computer."
            }
            Note {
                visible: card.stage === "done" && (card.result.settingsFiles || []).length > 0
                text: {
                    const n = (card.result.settingsFiles || []).length
                    return (n === 1 ? "1 settings file has" : n + " settings files have")
                        + " a new version beside yours. Yours were kept; ask Genie to compare them."
                }
            }
            Note {
                visible: card.stage === "done" && (card.result.failedServices || []).length > 0
                tone: theme.bad
                text: "Not running since: " + (card.result.failedServices || []).join(", ") + ". Ask Genie to look into it."
            }
            Note {
                visible: card.result.restorePoint >= 0
                tone: theme.good
                text: "Restore point " + card.result.restorePoint + " was taken just before, if anything seems wrong."
            }
        }

        Fold {
            width: body.width
            title: card.stage === "updating" || card.stage === "done" || card.stage === "failed" ? "Show what it did" : "Show every package"
            body: card.stage === "ready" ? (card.plan.packages || "")
                : card.stage === "updating" || card.stage === "done" || card.stage === "failed" ? card.job.log : ""
        }

        Flow {
            width: body.width
            spacing: 10
            visible: card.stage !== "looking" && card.stage !== "updating"
            Pill {
                objectName: "updateButton"
                visible: card.stage === "ready"
                implicitHeight: 52
                padding: 30
                fontSize: 16
                filled: true
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
}
