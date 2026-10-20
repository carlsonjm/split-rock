// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Tidy up Downloads card: what has piled up in Downloads and on the
// Desktop, in groups that open to each file and its own tick; then one tap
// moves what is ticked to the Trash (src/app/TidyUp.h).
CardFrame {
    id: card
    objectName: "tidyCard"
    readonly property var job: helpers.tidy
    readonly property string stage: card.job.stage
    readonly property var groups: card.job.groups
    // The groups opened to their files, by id.
    property var opened: ({})
    hue: card.stage === "failed" ? "coral" : "teal"

    // A box to tick, 20 square.
    component Tick: Rectangle {
        property bool checked
        property color tone
        width: 20; height: 20; radius: 4
        color: checked ? tone : "transparent"
        border.width: checked ? 0 : 2
        border.color: theme.edge
        Behavior on color { ColorAnimation { duration: 120 } }
        Icon {
            visible: parent.checked
            anchors.centerIn: parent
            width: 14; height: 14
            stroke: 2.4
            path: Icons.check
            color: theme.hueInk
        }
    }

    CardHead {
        headingObjectName: "tidyHeading"
        tone: card.tone
        icon: card.stage === "done" ? Icons.check : card.stage === "failed" ? Icons.alert : Icons.tidy
        spinning: card.stage === "looking"
        heading: {
            switch (card.stage) {
            case "looking": return "Looking through Downloads"
            case "nothing": return "Nothing to tidy up"
            case "moving": return "Moving to the Trash"
            case "done": return card.job.moved === 1 ? "1 file is in the Trash" : card.job.moved + " files are in the Trash"
            case "failed": return "Tidying up didn't finish"
            }
            return card.job.chosenKb > 0 ? card.job.chosenSize + " can go to the Trash" : "Pick what to move"
        }
        sub: {
            switch (card.stage) {
            case "looking": return "Downloads and the Desktop only. Nothing moves until you say."
            case "nothing": return "No copies, installers or old files are waiting in Downloads or on the Desktop."
            case "moving": return ""
            case "failed": return card.job.why
            case "done": return card.job.movedSize + ". Empty the Trash to free the space for good."
                                + (card.job.missed !== "" ? " " + card.job.missed : "")
            }
            return card.job.heldWords
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
        objectName: "tidyGroups"
        visible: card.stage === "ready"
        width: card.innerWidth
        spacing: 4
        Repeater {
            model: card.stage === "ready" ? card.groups : []
            Column {
                id: group
                required property var modelData
                readonly property bool open: card.opened[group.modelData.id] === true
                readonly property color tone: theme.hue(group.modelData.tone)
                width: card.innerWidth
                spacing: 4

                // The group: a tap on the box ticks every file; anywhere else
                // opens it to its files.
                Rectangle {
                    objectName: "tidy-" + group.modelData.id
                    width: parent.width
                    height: Math.max(56, groupText.implicitHeight + 24)
                    radius: theme.cardRadius
                    color: openArea.pressed ? theme.line : openArea.containsMouse ? theme.chip : theme.inset
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Accessible.role: Accessible.Button
                    Accessible.name: group.modelData.title

                    MouseArea {
                        id: openArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const next = Object.assign({}, card.opened)
                            next[group.modelData.id] = !group.open
                            card.opened = next
                        }
                    }
                    Item {
                        objectName: "tidyTick-" + group.modelData.id
                        x: 6; width: 44; height: parent.height
                        Tick {
                            anchors.centerIn: parent
                            checked: group.modelData.chosen
                            tone: group.tone
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: card.job.choose(group.modelData.id, !group.modelData.chosen)
                        }
                        function clicked() { card.job.choose(group.modelData.id, !group.modelData.chosen) }
                    }
                    Column {
                        id: groupText
                        x: 56
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - x - 12 - groupEnd.width - 12
                        spacing: 2
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: group.modelData.title
                            color: theme.text
                            font.pixelSize: 15
                            font.weight: Font.Medium
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: group.modelData.detail
                            color: theme.quiet
                            font.pixelSize: 13
                        }
                    }
                    Row {
                        id: groupEnd
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: group.modelData.size
                            color: group.modelData.chosen ? theme.text : theme.quiet
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                        }
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16; height: 16
                            rotation: group.open ? 0 : -90
                            path: Icons.chevron
                            color: theme.quiet
                            Behavior on rotation { NumberAnimation { duration: 120 } }
                        }
                    }
                }

                // Its files, each with its own tick, indented under it.
                Column {
                    visible: group.open
                    x: 32
                    width: parent.width - 32
                    spacing: 4
                    Repeater {
                        model: group.open ? group.modelData.files : []
                        Rectangle {
                            id: fileRow
                            required property var modelData
                            objectName: "tidyFile-" + fileRow.modelData.name
                            width: parent.width
                            height: 44
                            radius: theme.cardRadius
                            color: fileArea.pressed ? theme.line : fileArea.containsMouse ? theme.chip : theme.sheet
                            border.width: 1
                            border.color: theme.hairline
                            Accessible.role: Accessible.CheckBox
                            Accessible.name: fileRow.modelData.name
                            Accessible.checkable: true
                            Accessible.checked: fileRow.modelData.chosen
                            function clicked() { card.job.chooseFile(fileRow.modelData.path, !fileRow.modelData.chosen) }
                            MouseArea {
                                id: fileArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: fileRow.clicked()
                            }
                            Tick {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                checked: fileRow.modelData.chosen
                                tone: group.tone
                            }
                            Text {
                                x: 44
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - x - 12 - fileEnd.width - 12
                                elide: Text.ElideMiddle
                                text: fileRow.modelData.name
                                color: theme.text
                                font.pixelSize: 14
                            }
                            Text {
                                id: fileEnd
                                anchors.right: parent.right
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                width: Math.min(implicitWidth, parent.width * 0.55)
                                elide: Text.ElideLeft
                                text: fileRow.modelData.detail + " · " + fileRow.modelData.size
                                color: theme.quiet
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }

    CardFact {
        visible: card.stage === "ready"
        icon: Icons.history
        label: "Moved to the Trash, so anything can be taken back"
    }

    CardProgress {
        visible: card.stage === "moving"
        barObjectName: "tidyProgress"
        tone: card.tone
        done: card.job.done
        total: card.job.total
        now: card.job.now
    }

    CardRow {
        objectName: "tidyTrash"
        visible: card.stage === "done"
        width: card.innerWidth
        icon: Icons.trash
        title: "Open the Trash"
        detail: "Anything there can be put back where it was"
        opens: true
        onClicked: card.job.openTrash()
    }

    Fold {
        width: card.innerWidth
        title: "Show what it did"
        body: card.stage === "done" || card.stage === "failed" ? card.job.log : ""
    }

    Flow {
        width: card.innerWidth
        spacing: 8
        visible: !card.job.busy
        Pill {
            objectName: "tidyButton"
            visible: card.stage === "ready"
            implicitHeight: 40
            padding: 16
            fontSize: 14
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            enabled: card.job.chosenCount > 0
            iconPath: Icons.trash
            text: card.job.chosenKb > 0 ? "Move " + card.job.chosenSize + " to the Trash" : "Move to the Trash"
            onClicked: card.job.start()
        }
        Pill {
            visible: card.stage === "failed"
            implicitHeight: 40
            padding: 16
            fontSize: 14
            text: "Try again"
            onClicked: card.job.check()
        }
        Pill {
            objectName: "tidyClose"
            implicitHeight: 40
            padding: 16
            fontSize: 14
            text: card.stage === "ready" ? "Not now" : card.stage === "done" ? "Done" : "Close"
            onClicked: card.job.close()
        }
    }
}
