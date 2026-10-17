// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Install an app card: what the person needs, the apps for it with where
// each comes from, and one tap to install one; or, asked to remove an app, the
// installed apps that fit, and a yes before one is removed
// (src/app/AppInstall.h).
CardFrame {
    id: card
    objectName: "installCard"
    readonly property var job: helpers.install
    readonly property string stage: card.job.stage
    readonly property var results: card.job.results
    readonly property var picked: card.job.picked
    readonly property bool removing: card.job.removing
    hue: card.stage === "failed" ? "coral" : "violet"

    function search(text) {
        if (text.trim() !== "")
            card.job.search(text)
    }

    CardHead {
        headingObjectName: "installHeading"
        tone: card.tone
        icon: card.stage === "done" || card.stage === "removed" ? Icons.check : card.stage === "failed" ? Icons.alert
            : card.removing ? Icons.trash : Icons.download
        spinning: false
        heading: {
            switch (card.stage) {
            case "searching": return "Looking for " + card.job.query
            case "results":
                if (card.removing)
                    return card.results.length === 0 ? "Nothing installed fits “" + card.job.query + "”"
                                                     : "Remove which app?"
                return card.results.length === 0 ? "Nothing found for “" + card.job.query + "”"
                                                 : "Apps for “" + card.job.query + "”"
            case "confirm": return "Remove " + card.picked.name + "?"
            case "installing": return "Installing " + card.picked.name
            case "removing": return "Removing " + card.picked.name
            case "done": return card.picked.name + " is installed"
            case "removed": return card.picked.name + " is removed"
            case "failed": return card.removing ? "Removing didn't finish" : "Installing didn't finish"
            }
            return card.removing ? "Remove an app" : "Install an app"
        }
        sub: {
            switch (card.stage) {
            case "asking": return card.removing ? "Say which app, and " + productName + " finds it on this computer."
                                                : "Say what you need, and " + productName + " finds the apps for it."
            case "searching": return card.removing ? "Among the apps installed on this computer."
                                                   : "On this system's own shelf, and on Flathub."
            case "results":
                if (card.removing)
                    return card.results.length === 0 ? "Try its name as the app menu shows it. Parts the system needs aren't listed."
                                                     : "Tap the one to remove. Nothing is removed until you say yes."
                return card.results.length === 0 ? "Try other words, such as what you want to do with it."
                                                 : "This system's own shelf comes first: it's checked by the system's keys and updated with it."
            case "confirm": return card.picked.source === "shelf"
                                   ? "A restore point is taken first, and only this app is removed. If another app needs it, nothing is removed."
                                   : "It's removed from Flathub's apps on this computer. Its own settings stay."
            case "installing": return card.picked.source === "shelf" ? "From this system's shelf, with a restore point first."
                                                                    : "From Flathub."
            case "removing": return "You can keep working."
            case "done": return "It's in the app menu now."
            case "removed": return "To have it back, install it again from this card."
            case "failed": return card.job.why
            }
            return ""
        }
    }

    // What the person needs, in their words.
    Rectangle {
        visible: card.stage === "asking" || card.stage === "results" || card.stage === "failed"
        width: card.innerWidth
        height: 52
        radius: 26
        color: theme.inset
        border.width: 1
        border.color: queryField.activeFocus ? card.tone : theme.hairline
        Behavior on border.color { ColorAnimation { duration: 140 } }
        Icon {
            id: lens
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            width: 18; height: 18
            path: Icons.search
            color: theme.quiet
        }
        TextInput {
            id: queryField
            objectName: "installField"
            anchors.left: lens.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            clip: true
            text: card.job.query
            color: theme.text
            selectionColor: theme.palette.highlight
            selectedTextColor: theme.palette.highlightedText
            font.pixelSize: 16
            selectByMouse: true
            Accessible.role: Accessible.EditableText
            Accessible.name: "What do you need?"
            onAccepted: card.search(text)
            Component.onCompleted: if (card.stage === "asking") forceActiveFocus()
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                elide: Text.ElideRight
                visible: queryField.text === ""
                text: card.removing ? "Which app? Its name, as the app menu shows it" : "What do you need? A PDF editor, a photo app…"
                color: theme.quiet
                font: queryField.font
            }
        }
    }

    // Ways to start, while nothing is asked yet.
    Flow {
        visible: card.stage === "asking" && !card.removing
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: [{ text: "PDF editor", hue: "violet" }, { text: "Photo editor", hue: "pink" },
                    { text: "Video player", hue: "coral" }, { text: "Office suite", hue: "blue" },
                    { text: "Password manager", hue: "green" }]
            Rectangle {
                height: 36
                width: ideaText.implicitWidth + 30
                radius: 18
                color: ideaArea.pressed ? theme.soft(theme.hue(modelData.hue), 0.30) : theme.soft(theme.hue(modelData.hue))
                Accessible.role: Accessible.Button
                Accessible.name: modelData.text
                Text {
                    id: ideaText
                    anchors.centerIn: parent
                    text: modelData.text
                    color: theme.text
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                MouseArea {
                    id: ideaArea
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: card.search(modelData.text)
                }
            }
        }
    }

    // Searching: soft bars where the apps will be.
    Column {
        visible: card.stage === "searching"
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
        objectName: "installResults"
        visible: card.stage === "results" && card.results.length > 0
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: card.stage === "results" ? card.results : []
            CardRow {
                objectName: "install-" + index
                tone: modelData.source === "shelf" ? card.tone : theme.hue("teal")
                icon: modelData.source === "shelf" ? Icons.box : Icons.globe
                title: modelData.name
                detail: modelData.summary + (modelData.summary !== "" ? "\n" : "") + modelData.sourceName
                        + (modelData.alsoFlathub ? " · also on Flathub" : "")
                trailing: card.removing ? "Remove" : modelData.installed ? "Open" : "Install"
                opens: true
                Accessible.name: (card.removing ? "Remove " : modelData.installed ? "Open " : "Install ") + modelData.name
                onClicked: card.removing ? card.job.remove(modelData.key)
                         : modelData.installed ? card.job.openApp(modelData.key) : card.job.install(modelData.key)
            }
        }
    }

    CardFact {
        visible: card.stage === "results" && card.results.some(r => r.source === "shelf" && (card.removing || !r.installed))
        icon: Icons.lock
        label: "Apps from this system's shelf need your password"
    }

    CardProgress {
        visible: card.stage === "installing" || card.stage === "removing"
        barObjectName: "installProgress"
        tone: card.tone
        done: card.job.done
        total: card.job.total
        now: card.job.now
    }

    Fold {
        width: card.innerWidth
        title: "Show what it did"
        body: card.stage === "done" || card.stage === "removed" || card.stage === "failed" ? card.job.log : ""
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: !card.job.busy
        Pill {
            objectName: "openApp"
            visible: card.stage === "done"
            implicitHeight: 52
            padding: 28
            fontSize: 16
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            iconPath: Icons.external
            text: "Open " + card.picked.name
            onClicked: card.job.openApp()
        }
        Pill {
            visible: card.stage === "asking"
            implicitHeight: 52
            padding: 28
            fontSize: 16
            filled: true
            tone: card.tone
            toneInk: theme.hueInk
            enabled: queryField.text.trim() !== ""
            iconPath: Icons.search
            text: card.removing ? "Find it" : "Find apps"
            onClicked: card.search(queryField.text)
        }
        Pill {
            objectName: "confirmRemove"
            visible: card.stage === "confirm"
            implicitHeight: 52
            padding: 28
            fontSize: 16
            filled: true
            tone: theme.bad
            toneInk: theme.hueInk
            iconPath: Icons.trash
            text: "Remove"
            onClicked: card.job.confirmRemove()
        }
        Pill {
            visible: card.stage === "confirm"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: "Back"
            onClicked: card.job.back()
        }
        Pill {
            visible: card.stage === "done"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: "Find another"
            onClicked: card.job.ask()
        }
        Pill {
            objectName: "removeInstead"
            visible: card.stage === "asking" && !card.removing
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: "Remove an app instead"
            onClicked: card.job.askRemove()
        }
        Pill {
            objectName: "installClose"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            visible: card.stage !== "confirm"
            text: card.stage === "asking" || card.stage === "results" ? "Not now" : "Close"
            onClicked: card.job.close()
        }
    }
}
