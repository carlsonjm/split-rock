// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The full view, as docs/mockup/Workspace.dc.html draws it: the conversation,
// and This Device in a drawer at the right that folds away. A wide window
// opens with the notebook beside the conversation; a narrow one, such as a
// Bento column, opens with the conversation alone, and the notebook slides over
// it when asked for. Choosing an assistant, signing in and the Do it card
// happen in the conversation's column.
Item {
    id: full

    readonly property bool canAsk: conversation.phase === "ready"
    readonly property bool working: conversation.phase === "working"

    // This Device's closets, each folded until tapped. The device's
    // troubleshooting starts open while it has a problem still open.
    property bool systemOpen: false
    property bool aboutOpen: false
    // Open while the device has a problem still open; the person folds it.
    property bool troubleOpen: false
    Component.onCompleted: troubleOpen = conversation.deviceStillOpen.length > 0
    Connections {
        target: conversation
        function onNotebookChanged() {
            if (conversation.deviceStillOpen.length > 0)
                full.troubleOpen = true
        }
    }
    property bool changesOpen: false
    // The closet the card open in the conversation brings to the top.
    readonly property string paired: ({ changes: "changes", health: "trouble", restore: "system" })[conversation.chore] || ""

    function closetOpen(name) {
        return ({ system: systemOpen, about: aboutOpen, trouble: troubleOpen, changes: changesOpen })[name] || false
    }
    function toggleCloset(name) {
        if (name === "system") systemOpen = !systemOpen
        else if (name === "about") aboutOpen = !aboutOpen
        else if (name === "trouble") troubleOpen = !troubleOpen
        else if (name === "changes") changesOpen = !changesOpen
    }
    function closetTitle(name) {
        return ({ system: "System", about: "About me", trouble: "Troubleshooting", changes: "Changes" })[name] || ""
    }
    // What a folded closet holds, in a word or a number.
    function closetCount(name) {
        if (name === "trouble")
            return conversation.deviceStillOpen.length > 0 ? conversation.deviceStillOpen.length + " open" : ""
        if (name === "changes")
            return conversation.changes.length > 0 ? "" + conversation.changes.length : ""
        return ""
    }

    // Too narrow for the two side by side: the notebook covers the conversation.
    readonly property bool narrow: width < 720
    // Open beside the conversation when there is room for both, until the
    // person opens or folds it themselves.
    property bool notebookChosen: false
    property bool notebookChoice: false
    readonly property bool notebookShown: notebookChosen ? notebookChoice : width >= 960
    readonly property real notebookWidth: narrow ? width : Math.round(Math.min(460, Math.max(320, width * 0.36)))
    property real notebookProgress: notebookShown ? 1 : 0
    Behavior on notebookProgress { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    function toggleNotebook() {
        notebookChoice = !notebookShown
        notebookChosen = true
    }

    // "4 October" this year; with the year otherwise.
    function shortDate(when) {
        const year = " " + new Date().getFullYear()
        return when.endsWith(year) ? when.slice(0, -year.length) : when
    }

    function focusField() {
        wideField.forceActiveFocus()
    }

    // A problem still open, handed to the assistant to write up its fix: the
    // field holds the start of the sentence, for the person to finish.
    function tellFix(problem) {
        if (narrow) {
            notebookChoice = false
            notebookChosen = true
        }
        wideField.text = "Solved: " + problem + " What fixed it: "
        wideField.cursorPosition = wideField.text.length
        wideField.forceActiveFocus()
    }

    function send() {
        if (working) {
            conversation.stop()
        } else if (canAsk && wideField.text.trim() !== "") {
            conversation.ask(wideField.text)
            wideField.text = ""
        }
    }

    // An answer: no box, the assistant's mark beside it and the words set
    // for reading, as a page rather than a bubble.
    component Answer: Item {
        id: answer
        property string headline
        property var steps: []
        property var more: []
        property bool waiting: false
        width: talk.reading
        height: Math.max(28, answerColumn.implicitHeight)
        Mark {
            width: 28; height: 28
            radius: 9
        }
        Column {
            id: answerColumn
            x: 42; y: 3
            width: Math.min(640, parent.width - 42)
            spacing: 10
            // Thinking: three dots and the word, so a wait never reads as
            // an answer that went nowhere.
            Row {
                objectName: "thinking"
                visible: answer.waiting
                height: 22
                spacing: 7
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    rightPadding: 4
                    text: "Thinking"
                    color: theme.quiet
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: 3
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 7; height: 7; radius: 3.5
                        color: theme.accent
                        SequentialAnimation on opacity {
                            loops: Animation.Infinite
                            PauseAnimation { duration: index * 160 }
                            NumberAnimation { from: 0.25; to: 1; duration: 420 }
                            NumberAnimation { from: 1; to: 0.25; duration: 420 }
                            PauseAnimation { duration: (2 - index) * 160 }
                        }
                    }
                }
            }
            Text {
                visible: text !== ""
                width: parent.width
                wrapMode: Text.WordWrap
                text: answer.headline
                color: theme.text
                font.pixelSize: 17
                font.weight: Font.Bold
                lineHeight: 1.35
            }
            Repeater {
                model: answer.steps
                Row {
                    width: answerColumn.width
                    spacing: 12
                    Rectangle {
                        y: 1
                        width: 24; height: 24; radius: 12
                        color: theme.accentSoft
                        Text {
                            anchors.centerIn: parent
                            text: index + 1
                            color: theme.accent
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }
                    }
                    Text {
                        width: parent.width - 36
                        wrapMode: Text.WordWrap
                        text: modelData
                        color: theme.text
                        font.pixelSize: 16
                        lineHeight: 1.45
                    }
                }
            }
            Repeater {
                model: answer.more
                Text {
                    width: answerColumn.width
                    wrapMode: Text.WordWrap
                    text: modelData
                    color: theme.text
                    font.pixelSize: 16
                    lineHeight: 1.45
                }
            }
        }
    }

    // What the person asked: a bubble at the right, in the conversation's
    // colours.
    component Asked: Item {
        property string text
        width: talk.reading
        height: askedText.implicitHeight + 24
        Rectangle {
            anchors.right: parent.right
            width: Math.min(Math.min(520, Math.round(parent.width * 0.8)), askedText.implicitWidth + 36)
            height: parent.height
            radius: theme.bubbleRadius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: theme.accent }
                GradientStop { position: 1; color: theme.accentTo }
            }
            Text {
                id: askedText
                x: 18; y: 12
                width: parent.width - 36
                wrapMode: Text.WordWrap
                text: parent.parent.text
                color: theme.accentInk
                font.pixelSize: 16
                lineHeight: 1.3
            }
        }
    }

    // A fix the device's own page holds: its problem, and, tapped open, why,
    // what to do, and Came back, for a problem the fix no longer holds.
    component DeviceFix: Rectangle {
        id: fix
        property string path
        property string title
        property string why
        property var steps: []
        property bool open: false
        width: parent ? parent.width : 0
        height: fixColumn.implicitHeight + 24
        radius: 14
        color: theme.raised
        border.width: 1
        border.color: theme.hairline
        Accessible.role: Accessible.Button
        Accessible.name: title
        Column {
            id: fixColumn
            x: 14; y: 12
            width: parent.width - 28
            spacing: 8
            Item {
                width: parent.width
                height: Math.max(fixTitle.implicitHeight, 24)
                Text {
                    id: fixTitle
                    width: parent.width - 34
                    wrapMode: Text.WordWrap
                    text: fix.title
                    color: theme.text
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    lineHeight: 1.3
                }
                Icon {
                    anchors.right: parent.right
                    y: 2
                    width: 18; height: 18
                    path: Icons.chevron
                    color: theme.quiet
                    rotation: fix.open ? 180 : 0
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: fix.open = !fix.open
                }
            }
            Text {
                visible: fix.open && fix.why !== ""
                width: parent.width
                wrapMode: Text.WordWrap
                text: fix.why
                color: theme.quiet
                font.pixelSize: 14
                lineHeight: 1.3
            }
            Repeater {
                model: fix.open ? fix.steps : []
                Text {
                    width: fixColumn.width
                    wrapMode: Text.WordWrap
                    text: (index + 1) + ". " + modelData
                    color: theme.text
                    font.pixelSize: 14
                    lineHeight: 1.3
                }
            }
            Pill {
                objectName: "cameBack"
                visible: fix.open
                implicitHeight: 40
                text: "Came back"
                onClicked: conversation.reopenFix(fix.path)
            }
        }
    }

    // A problem the device still has. Tapped open: hand it to the assistant
    // to write up the fix, or write the fix here and mark it solved.
    component OpenProblem: Rectangle {
        id: problem
        property string text
        property bool open: false
        property bool writing: false
        width: parent ? parent.width : 0
        height: problemColumn.implicitHeight + 24
        radius: 14
        color: theme.raised
        border.width: 1
        border.color: theme.hairline
        Column {
            id: problemColumn
            x: 14; y: 12
            width: parent.width - 28
            spacing: 10
            Item {
                width: parent.width
                height: Math.max(problemText.implicitHeight, 24)
                Text {
                    id: problemText
                    width: parent.width - 34
                    wrapMode: Text.WordWrap
                    text: problem.text
                    color: theme.text
                    font.pixelSize: 15
                    lineHeight: 1.3
                }
                Icon {
                    anchors.right: parent.right
                    y: 2
                    width: 18; height: 18
                    path: Icons.chevron
                    color: theme.quiet
                    rotation: problem.open ? 180 : 0
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: problem.text
                    onClicked: {
                        problem.open = !problem.open
                        problem.writing = false
                    }
                }
            }
            Flow {
                visible: problem.open && !problem.writing
                width: parent.width
                spacing: 8
                Pill {
                    objectName: "tellFix"
                    implicitHeight: 40
                    visible: !conversation.isGuide && full.canAsk
                    text: "Tell " + productName + " the fix"
                    onClicked: full.tellFix(problem.text)
                }
                Pill {
                    objectName: "writeFix"
                    implicitHeight: 40
                    text: "Write the fix"
                    onClicked: {
                        problem.writing = true
                        fixField.forceActiveFocus()
                    }
                }
            }
            Rectangle {
                visible: problem.writing
                width: parent.width
                height: Math.max(88, fixField.implicitHeight + 24)
                radius: 12
                color: theme.sheet
                border.width: 1
                border.color: fixField.activeFocus ? theme.text : theme.edge
                TextEdit {
                    id: fixField
                    objectName: "fixField"
                    x: 12; y: 12
                    width: parent.width - 24
                    wrapMode: TextEdit.Wrap
                    color: theme.text
                    selectionColor: theme.palette.highlight
                    selectedTextColor: theme.palette.highlightedText
                    font.pixelSize: 15
                    selectByMouse: true
                    Accessible.role: Accessible.EditableText
                    Accessible.name: "What fixed it"
                    Text {
                        visible: fixField.text === ""
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: "What fixed it, in your own words. A line for each step."
                        color: theme.quiet
                        font: fixField.font
                    }
                }
            }
            Flow {
                visible: problem.writing
                width: parent.width
                spacing: 8
                Pill {
                    objectName: "markSolved"
                    implicitHeight: 40
                    filled: true
                    enabled: fixField.text.trim() !== ""
                    text: "Mark solved"
                    onClicked: if (conversation.solveStillOpen(problem.text, fixField.text)) problem.open = false
                }
                Pill {
                    implicitHeight: 40
                    text: "Cancel"
                    onClicked: problem.writing = false
                }
            }
        }
    }

    component SubHeading: Text {
        color: theme.quiet
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    component Heading: Text {
        color: theme.quiet
        font.pixelSize: 12
        font.weight: Font.ExtraBold
        font.letterSpacing: 0.7
    }

    component Tile: Rectangle {
        property string label
        property string value
        height: 62
        radius: 14
        color: theme.raised
        border.width: 1
        border.color: theme.hairline
        Column {
            x: 14; y: 12
            width: parent.width - 28
            spacing: 4
            Text {
                text: parent.parent.label
                color: theme.quiet
                font.pixelSize: 11
                font.weight: Font.ExtraBold
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: parent.parent.value
                color: theme.text
                font.pixelSize: 15
                font.weight: Font.Bold
            }
        }
    }

    component Line: Rectangle {
        id: line
        property string when
        property string text
        property string status
        // A button at the row's end, where the mock-up puts Undo.
        property string action
        signal acted()
        width: parent ? parent.width : 0
        // Too narrow for the date beside the words: it goes above them.
        readonly property bool stacked: width < 400 && when !== ""
        height: stacked ? lineText.implicitHeight + 46 : Math.max(48, lineText.implicitHeight + 24)
        radius: 14
        color: theme.raised
        border.width: 1
        border.color: theme.hairline
        Text {
            x: 14; y: 13
            width: line.stacked ? implicitWidth : 86
            visible: text !== ""
            text: parent.when
            color: theme.quiet
            font.pixelSize: 12
            font.weight: Font.Bold
        }
        Text {
            id: lineText
            x: parent.when !== "" && !line.stacked ? 114 : 14
            y: line.stacked ? 34 : 12
            width: parent.width - x - 14 - (statusText.visible && !line.stacked ? statusText.implicitWidth + 14 : 0)
                   - (actionButton.visible ? actionButton.width + 14 : 0)
            wrapMode: Text.WordWrap
            text: parent.text
            color: theme.text
            font.pixelSize: 15
            lineHeight: 1.3
        }
        Rectangle {
            id: actionButton
            visible: line.action !== ""
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: actionLabel.implicitWidth + 32
            height: 40
            radius: 20
            color: actionArea.pressed ? theme.line : theme.chip
            border.width: 1
            border.color: theme.edge
            Accessible.role: Accessible.Button
            Accessible.name: line.action + " " + line.text
            Text {
                id: actionLabel
                anchors.centerIn: parent
                text: line.action
                color: theme.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }
            MouseArea {
                id: actionArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: line.acted()
            }
        }
        Text {
            id: statusText
            anchors.right: actionButton.visible ? actionButton.left : parent.right
            anchors.rightMargin: 14
            y: line.stacked ? 13 : 15
            visible: text !== ""
            text: parent.status
            color: theme.quiet
            font.pixelSize: 12
        }
    }

    // The conversation: what was asked in this window, and its answers.
    Item {
        id: talk
        // Beside the notebook, or the whole width when it is folded away or
        // slides over.
        width: full.narrow ? full.width : full.width - (full.notebookWidth + 25) * full.notebookProgress
        height: parent.height
        // What the conversation's bubbles and field fill: all of it, up to a
        // comfortable reading width, centred.
        readonly property real gap: full.narrow ? 0 : 18 * full.notebookProgress
        readonly property real reading: Math.min(width - gap, 760)
        readonly property real readingX: Math.max(0, (width - gap - reading) / 2)

        // Which assistant is answering, and the way to another, as in the
        // card.
        Pill {
            id: assistantButton
            objectName: "fullAssistantPill"
            x: talk.readingX
            implicitHeight: 40
            visible: conversation.assistantId !== "" && !conversation.choosing
            enabled: conversation.phase !== "working" && conversation.phase !== "signingIn"
            text: conversation.assistantName + "  ▾"
            onClicked: conversation.showAssistants()
        }

        // Opens the notebook, while it is folded away.
        Pill {
            id: notebookButton
            objectName: "notebookButton"
            x: talk.readingX + talk.reading - width
            implicitHeight: 40
            opacity: 1 - full.notebookProgress
            visible: opacity > 0
            text: "This Device"
            iconPath: Icons.panel
            onClicked: full.toggleNotebook()
        }

        Flickable {
            id: history
            y: notebookButton.visible || assistantButton.visible ? notebookButton.height + 12 : 0
            width: parent.width
            height: wideBox.y - 12 - y
            contentWidth: width
            contentHeight: bubbles.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            WheelScroll { flickable: history }
            function toEnd() {
                contentY = Math.max(0, contentHeight - height)
            }
            onContentHeightChanged: toEnd()
            onHeightChanged: toEnd()
            // A new question, an answer starting, or a card closing moves the
            // conversation, once laid out, to its newest part.
            Connections {
                target: conversation
                function onAnswerChanged() { Qt.callLater(history.toEnd) }
                function onPhaseChanged() { Qt.callLater(history.toEnd) }
                function onChoreChanged() { Qt.callLater(history.toEnd) }
            }

            Column {
                id: bubbles
                x: talk.readingX
                width: talk.reading
                spacing: 22
                topPadding: 8

                Text {
                    visible: conversation.turns.length === 0 && conversation.question === "" && !step.item
                    width: talk.reading
                    wrapMode: Text.WordWrap
                    text: "Ask " + (conversation.assistantName !== "" ? conversation.assistantName : productName)
                          + " anything about this computer. This conversation lasts until you start over or log out."
                    color: theme.quiet
                    font.pixelSize: 15
                    lineHeight: 1.3
                }

                // Before anything is asked: the upkeep cards, to start from.
                Column {
                    objectName: "welcomeShelf"
                    visible: conversation.turns.length === 0 && conversation.question === "" && !step.item
                             && !conversation.choosing
                    width: talk.reading
                    spacing: 12
                    Heading { text: "LOOK AFTER THIS COMPUTER" }
                    UpkeepShelf { width: parent.width }
                }

                Repeater {
                    model: conversation.turns
                    Column {
                        spacing: 22
                        Asked { text: modelData.question }
                        Answer {
                            headline: modelData.headline
                            steps: modelData.steps
                            more: modelData.more
                        }
                    }
                }

                Asked {
                    visible: conversation.question !== ""
                    text: conversation.question
                }
                Answer {
                    visible: conversation.question !== "" && !conversation.choosing
                    headline: conversation.headline
                    steps: conversation.steps
                    more: conversation.more
                    waiting: full.working && conversation.headline === "" && conversation.steps.length === 0
                }

                // Under the answer, as under the card's: Keep this, That fixed
                // it, and what the assistant offers to remember.
                Column {
                    objectName: "fullAnswerActions"
                    width: talk.reading
                    spacing: 10
                    visible: conversation.question !== "" && full.canAsk && !conversation.choosing
                             && !conversation.cardOpen && conversation.canKeep
                    Flow {
                        width: parent.width
                        spacing: 8
                        Pill {
                            implicitHeight: 40
                            enabled: !conversation.kept
                            text: conversation.kept ? "Kept" : "Keep this"
                            onClicked: conversation.keep()
                        }
                        Pill {
                            implicitHeight: 40
                            visible: conversation.nextTour === ""
                            enabled: !conversation.fixed
                            text: conversation.fixed ? "Fixed" : "That fixed it"
                            onClicked: conversation.markFixed()
                        }
                    }
                    RememberLine {
                        width: parent.width
                        visible: conversation.remember !== ""
                    }
                }

                // What is asked of the person now: an assistant to choose, a
                // way to sign in, a change to decide on.
                Loader {
                    id: step
                    objectName: "fullStep"
                    width: talk.reading
                    sourceComponent: {
                        if (conversation.choosing)
                            return chooseStep
                        // An upkeep card the person opened needs no
                        // assistant, so it comes before the assistant's own
                        // steps.
                        switch (conversation.chore) {
                        case "update": return updateStep
                        case "space": return spaceStep
                        case "install": return installStep
                        case "restore": return restoreStep
                        case "health": return healthStep
                        case "community": return communityStep
                        case "changes": return changesStep
                        }
                        switch (conversation.phase) {
                        case "download": return downloadStep
                        case "starting": return startingStep
                        case "signIn":
                        case "signingIn": return signInStep
                        case "problem": return problemStep
                        default: return conversation.cardOpen ? doItStep : null
                        }
                    }
                }

                Text {
                    visible: conversation.notice !== "" && !conversation.choosing
                    width: talk.reading
                    wrapMode: Text.WordWrap
                    text: conversation.notice
                    color: theme.text
                    font.pixelSize: 14
                    lineHeight: 1.3
                }
            }
        }

        Rectangle {
            id: wideBox
            x: talk.readingX
            y: parent.height - height
            width: talk.reading
            height: 58
            radius: 29
            color: theme.raised
            border.width: 1
            border.color: wideField.activeFocus && full.canAsk ? theme.accent : theme.hairline
            opacity: full.canAsk || full.working ? 1 : 0.5
            Behavior on border.color { ColorAnimation { duration: 140 } }

            TextInput {
                id: wideField
                objectName: "wideField"
                anchors.left: parent.left
                anchors.leftMargin: 22
                anchors.right: wideSend.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                color: theme.text
                selectionColor: theme.palette.highlight
                selectedTextColor: theme.palette.highlightedText
                font.pixelSize: 16
                selectByMouse: true
                Accessible.role: Accessible.EditableText
                Accessible.name: "Ask " + productName
                onAccepted: full.send()
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    elide: Text.ElideRight
                    visible: wideField.text === ""
                    text: full.working ? "Thinking… the square stops it" : "Ask, or say what you want done"
                    color: theme.quiet
                    font: wideField.font
                }
            }

            Rectangle {
                id: wideSend
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 42; height: 42; radius: 21
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: theme.accent }
                    GradientStop { position: 1; color: theme.accentTo }
                }
                opacity: full.working || (full.canAsk && wideField.text.trim() !== "") ? 1 : 0.38
                Accessible.role: Accessible.Button
                Accessible.name: full.working ? "Stop" : "Send"
                Icon {
                    anchors.centerIn: parent
                    width: 16; height: 16
                    path: full.working ? Icons.stop : Icons.arrow
                    stroke: 2.2
                    color: theme.accentInk
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: full.send()
                }
            }
        }
    }

    Rectangle {
        visible: !full.narrow && full.notebookProgress > 0
        x: talk.width
        width: 1
        height: parent.height
        color: theme.line
        opacity: full.notebookProgress
    }

    // Under the notebook where it slides over the conversation, so the
    // conversation does not show through.
    Rectangle {
        visible: full.narrow && full.notebookProgress > 0
        x: aside.x
        width: full.width - x
        height: parent.height
        color: theme.sheet
    }

    // The drawer: This Device. The upkeep tiles on top; under them the
    // closets, each folded until tapped. The card open in the conversation
    // brings the closet that goes with it to the top, open: What changed its
    // changes, the health check the device's troubleshooting, restore points
    // the system.
    Flickable {
        id: aside
        objectName: "notebook"
        x: full.width - full.notebookWidth * full.notebookProgress
        width: full.notebookWidth
        height: parent.height
        visible: full.notebookProgress > 0
        contentWidth: width
        contentHeight: side.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll { flickable: aside }

        Column {
            id: side
            width: parent.width
            spacing: 16

            Item {
                width: parent.width
                height: 40
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "This Device"
                    color: theme.text
                    font.pixelSize: 17
                    font.weight: Font.Bold
                }
                // Folds the drawer away.
                Rectangle {
                    id: foldButton
                    objectName: "foldNotebook"
                    anchors.right: parent.right
                    width: 40; height: 40; radius: 20
                    color: foldArea.containsMouse ? theme.chip : "transparent"
                    Accessible.role: Accessible.Button
                    Accessible.name: "Fold This Device away"
                    Icon {
                        anchors.centerIn: parent
                        width: 18; height: 18
                        path: full.narrow ? Icons.close : Icons.panel
                        color: theme.text
                    }
                    MouseArea {
                        id: foldArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: full.toggleNotebook()
                    }
                }
            }

            // The closet that goes with the card open now, on top and open.
            Closet {
                objectName: "pairedCloset"
                visible: full.paired !== ""
                name: full.paired
                pinned: true
            }

            // The upkeep cards, opened in the conversation.
            Heading { text: "LOOK AFTER IT" }
            UpkeepShelf {
                width: parent.width
                onOpened: if (full.narrow) full.toggleNotebook()
            }

            Repeater {
                model: ["system", "about", "trouble", "changes"]
                Closet {
                    visible: modelData !== full.paired
                    name: modelData
                }
            }
        }
    }

    // A closet: a heading that folds what it holds away, and what it holds.
    component Closet: Column {
        id: closet
        property string name
        // Shown open with the card it goes with; its heading doesn't fold it.
        property bool pinned: false
        readonly property bool open: pinned || full.closetOpen(name)
        width: parent ? parent.width : 0
        spacing: 10

        Item {
            width: closet.width
            height: 40
            Accessible.role: Accessible.Button
            Accessible.name: full.closetTitle(closet.name)
            Heading {
                anchors.verticalCenter: parent.verticalCenter
                text: full.closetTitle(closet.name).toUpperCase()
            }
            Text {
                anchors.right: chevron.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                visible: !closet.open && text !== ""
                text: full.closetCount(closet.name)
                color: theme.quiet
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Icon {
                id: chevron
                visible: !closet.pinned
                anchors.right: parent.right
                anchors.rightMargin: 11
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 18
                path: Icons.chevron
                color: theme.quiet
                rotation: closet.open ? 180 : 0
            }
            MouseArea {
                anchors.fill: parent
                enabled: !closet.pinned
                cursorShape: closet.pinned ? Qt.ArrowCursor : Qt.PointingHandCursor
                onClicked: full.toggleCloset(closet.name)
            }
        }

        Loader {
            active: closet.open
            visible: active
            onLoaded: item.width = Qt.binding(() => closet.width)
            sourceComponent: ({ system: systemCloset, about: aboutCloset, trouble: troubleCloset,
                                changes: changesCloset })[closet.name] || null
        }

        Rectangle {
            visible: !closet.pinned
            width: closet.width
            height: 1
            color: theme.line
        }
    }

    // System: the computer, its system, and its newest restore point.
    Component {
        id: systemCloset
        Grid {
            id: tiles
            columns: width >= 560 ? 3 : 1
            spacing: 10
            Tile {
                width: (tiles.width - (tiles.columns - 1) * 10) / tiles.columns
                label: "COMPUTER"
                value: computerName !== "" ? computerName : "Not reported"
            }
            Tile {
                width: (tiles.width - (tiles.columns - 1) * 10) / tiles.columns
                label: "SYSTEM"
                value: systemName
            }
            Tile {
                objectName: "lastRestorePoint"
                width: (tiles.width - (tiles.columns - 1) * 10) / tiles.columns
                label: "LAST RESTORE POINT"
                value: conversation.restore.newest !== "" ? conversation.restore.newest : "Tap to see"
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: if (conversation.openChore("restore") && full.narrow) full.toggleNotebook()
                }
            }
        }
    }

    // About me: how the person likes to be helped.
    Component {
        id: aboutCloset
        Line {
            text: conversation.aboutMe !== "" ? conversation.aboutMe
                : "How you like to be helped, and who helps you. " + productName
                  + " writes this only after you say yes."
        }
    }

    // Troubleshooting: what this device still has wrong, then what fixed
    // what it had.
    Component {
        id: troubleCloset
        Column {
            objectName: "deviceKnown"
            spacing: 8
            Line {
                visible: conversation.deviceFixes.length === 0 && conversation.deviceStillOpen.length === 0
                text: "Nothing learned about this device yet. Its problems, and what fixed them, are kept here."
            }
            SubHeading {
                visible: conversation.deviceStillOpen.length > 0
                text: "Open"
            }
            Repeater {
                model: conversation.deviceStillOpen
                OpenProblem {
                    objectName: "deviceOpen-" + index
                    text: modelData
                }
            }
            SubHeading {
                visible: conversation.deviceFixes.length > 0
                topPadding: conversation.deviceStillOpen.length > 0 ? 8 : 0
                text: "Solved"
            }
            Repeater {
                model: conversation.deviceFixes
                DeviceFix {
                    objectName: "deviceFix-" + index
                    path: modelData.path
                    title: modelData.title
                    why: modelData.why
                    steps: modelData.steps
                }
            }
        }
    }

    // Changes: what Split Rock changed, newest first, each with Undo while it
    // can be put back.
    Component {
        id: changesCloset
        Column {
            objectName: "changes"
            spacing: 8
            Line {
                visible: conversation.refusedChanges.length === 0 && conversation.changes.length === 0
                text: "Nothing has changed on this computer."
            }
            Repeater {
                model: conversation.changes
                Line {
                    objectName: "change-" + index
                    when: full.shortDate(modelData.date)
                    text: modelData.title
                    status: ({ making: "Making it", done: "Done", failed: "Didn't work",
                               undone: "Undone", undoFailed: "Undo didn't work" })[modelData.status] || ""
                    action: modelData.canUndo && !conversation.changing ? "Undo" : ""
                    onActed: conversation.undoChange(modelData.id)
                }
            }
            Repeater {
                model: conversation.refusedChanges
                Line {
                    when: "Just now"
                    text: modelData.heading
                    status: "Asked, not changed"
                }
            }
        }
    }

    Component { id: chooseStep; AssistantList {} }
    Component { id: startingStep; StartingPanel {} }
    Component { id: downloadStep; DownloadPanel {} }
    Component { id: signInStep; SignInPanel {} }
    Component { id: problemStep; ProblemPanel {} }
    Component { id: doItStep; DoItCard {} }
    Component { id: updateStep; UpdateCard {} }
    Component { id: spaceStep; SpaceCard {} }
    Component { id: installStep; InstallCard {} }
    Component { id: restoreStep; RestoreCard {} }
    Component { id: healthStep; HealthCard {} }
    Component { id: communityStep; CommunityCard {} }
    Component { id: changesStep; ChangesCard {} }
}
