// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The full view, as docs/mockup/Workspace.dc.html draws it: the conversation,
// and the notebook in a panel at the right that folds away. A wide window
// opens with the notebook beside the conversation; a narrow one, such as a
// Bento column, opens with the conversation alone, and the notebook slides over
// it when asked for. Choosing an assistant, signing in and the Do it card
// happen in the conversation's column.
Item {
    id: full

    readonly property bool canAsk: conversation.phase === "ready"
    readonly property bool working: conversation.phase === "working"
    property int tab: 0

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

    component Answer: Rectangle {
        property string headline
        property var steps: []
        property var more: []
        property bool waiting: false
        width: Math.min(640, talk.reading)
        height: answerColumn.implicitHeight + 24
        radius: 18
        color: theme.panel
        Column {
            id: answerColumn
            x: 14; y: 12
            width: parent.width - 28
            spacing: 8
            Row {
                visible: parent.parent.waiting
                spacing: 8
                Repeater {
                    model: 3
                    Rectangle {
                        width: 7; height: 7; radius: 3.5
                        color: theme.quiet
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
                text: parent.parent.headline
                color: theme.text
                font.pixelSize: 15
                font.weight: Font.Bold
                lineHeight: 1.3
            }
            Repeater {
                model: answerColumn.parent.steps
                Text {
                    width: answerColumn.width
                    wrapMode: Text.WordWrap
                    text: (index + 1) + ". " + modelData
                    color: theme.text
                    font.pixelSize: 15
                    lineHeight: 1.3
                }
            }
            Repeater {
                model: answerColumn.parent.more
                Text {
                    width: answerColumn.width
                    wrapMode: Text.WordWrap
                    text: modelData
                    color: theme.text
                    font.pixelSize: 15
                    lineHeight: 1.3
                }
            }
        }
    }

    component Asked: Item {
        property string text
        width: talk.reading
        height: askedText.implicitHeight + 20
        Rectangle {
            anchors.right: parent.right
            width: Math.min(Math.min(520, Math.round(parent.width * 0.85)), askedText.implicitWidth + 28)
            height: parent.height
            radius: 18
            color: theme.primary
            Text {
                id: askedText
                x: 14; y: 10
                width: parent.width - 28
                wrapMode: Text.WordWrap
                text: parent.parent.text
                color: theme.primaryInk
                font.pixelSize: 15
                font.weight: Font.DemiBold
            }
        }
    }

    // A fix the device's own page holds: its problem, and, tapped open, why
    // and what to do.
    component DeviceFix: Rectangle {
        id: fix
        property string title
        property string why
        property var steps: []
        property bool open: false
        width: parent ? parent.width : 0
        height: fixColumn.implicitHeight + 24
        radius: 12
        color: theme.panel
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
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: fix.open = !fix.open
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
        radius: 12
        color: theme.panel
        Column {
            id: problemColumn
            x: 14; y: 12
            width: parent.width - 28
            spacing: 10
            Item {
                width: parent.width
                height: Math.max(problemText.implicitHeight, problemStatus.implicitHeight)
                Text {
                    id: problemText
                    width: parent.width - problemStatus.implicitWidth - 14
                    wrapMode: Text.WordWrap
                    text: problem.text
                    color: theme.text
                    font.pixelSize: 15
                    lineHeight: 1.3
                }
                Text {
                    id: problemStatus
                    anchors.right: parent.right
                    y: 3
                    text: "Still open"
                    color: theme.quiet
                    font.pixelSize: 12
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
        radius: 12
        color: theme.panel
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
        radius: 12
        color: theme.panel
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

        // Opens the notebook, while it is folded away.
        Pill {
            id: notebookButton
            objectName: "notebookButton"
            x: talk.readingX + talk.reading - width
            implicitHeight: 40
            opacity: 1 - full.notebookProgress
            visible: opacity > 0
            text: "Notebook"
            iconPath: Icons.panel
            onClicked: full.toggleNotebook()
        }

        Flickable {
            id: history
            y: notebookButton.visible ? notebookButton.height + 12 : 0
            width: parent.width
            height: wideBox.y - 12 - y
            contentWidth: width
            contentHeight: bubbles.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            onContentHeightChanged: contentY = Math.max(0, contentHeight - height)

            Column {
                id: bubbles
                x: talk.readingX
                width: talk.reading
                spacing: 12

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

                Repeater {
                    model: conversation.turns
                    Column {
                        spacing: 12
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
                        switch (conversation.phase) {
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
            height: 52
            radius: 26
            color: theme.panel
            border.width: 1
            border.color: wideField.activeFocus && full.canAsk ? theme.text : theme.edge
            opacity: full.canAsk || full.working ? 1 : 0.5

            TextInput {
                id: wideField
                objectName: "wideField"
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.right: wideSend.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                color: theme.text
                selectionColor: theme.palette.highlight
                selectedTextColor: theme.palette.highlightedText
                font.pixelSize: 15
                selectByMouse: true
                Accessible.role: Accessible.EditableText
                Accessible.name: "Ask " + productName
                onAccepted: full.send()
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    elide: Text.ElideRight
                    visible: wideField.text === ""
                    text: "Ask, or say what you want done"
                    color: theme.quiet
                    font: wideField.font
                }
            }

            Rectangle {
                id: wideSend
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40; radius: 20
                color: theme.primary
                opacity: full.working || (full.canAsk && wideField.text.trim() !== "") ? 1 : 0.38
                Accessible.role: Accessible.Button
                Accessible.name: full.working ? "Stop" : "Send"
                Icon {
                    anchors.centerIn: parent
                    width: 16; height: 16
                    path: full.working ? Icons.stop : Icons.arrow
                    color: theme.primaryInk
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

    // The notebook: this computer, about the person, and what was kept.
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

        Column {
            id: side
            width: parent.width
            spacing: 16

            Item {
                width: parent.width
                height: tabs.height
                Flow {
                    id: tabs
                    width: parent.width - foldButton.width - 8
                    spacing: 4
                    Repeater {
                        model: ["This computer", "About me", "Kept"]
                        Rectangle {
                            readonly property bool picked: full.tab === index
                            width: tabLabel.implicitWidth + 32
                            height: 40
                            radius: 20
                            color: picked ? theme.primary : tabArea.containsMouse ? theme.chip : "transparent"
                            Accessible.role: Accessible.PageTab
                            Accessible.name: modelData
                            Accessible.selected: picked
                            Text {
                                id: tabLabel
                                anchors.centerIn: parent
                                text: modelData
                                color: parent.picked ? theme.primaryInk : theme.text
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                            }
                            MouseArea {
                                id: tabArea
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: full.tab = index
                            }
                        }
                    }
                }
                // Folds the notebook away.
                Rectangle {
                    id: foldButton
                    objectName: "foldNotebook"
                    anchors.right: parent.right
                    width: 40; height: 40; radius: 20
                    color: foldArea.containsMouse ? theme.chip : "transparent"
                    Accessible.role: Accessible.Button
                    Accessible.name: "Fold the notebook away"
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

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: conversation.canKeep ? "Kept in your notebook, as plain notes." : "Nothing is kept yet."
                color: theme.quiet
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }

            // This computer.
            Column {
                visible: full.tab === 0
                width: parent.width
                spacing: 16

                Grid {
                    id: tiles
                    width: parent.width
                    columns: width >= 560 ? 3 : 1
                    spacing: 10
                    Tile {
                        width: (parent.width - (tiles.columns - 1) * 10) / tiles.columns
                        label: "COMPUTER"
                        value: computerName !== "" ? computerName : "Not reported"
                    }
                    Tile {
                        width: (parent.width - (tiles.columns - 1) * 10) / tiles.columns
                        label: "SYSTEM"
                        value: systemName
                    }
                    Tile {
                        width: (parent.width - (tiles.columns - 1) * 10) / tiles.columns
                        label: "LAST SNAPSHOT"
                        value: "None yet"
                    }
                }

                Heading { text: "CHANGES" }
                Column {
                    objectName: "changes"
                    width: parent.width
                    spacing: 8
                    Line {
                        visible: conversation.refusedChanges.length === 0
                        text: "Nothing has changed on this computer."
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

                Heading { text: "FIXED PROBLEMS" }
                Line {
                    visible: conversation.fixedProblems.length === 0
                    width: parent.width
                    text: "None yet. Problems " + productName + " helps fix are written down here."
                }
                Repeater {
                    model: conversation.fixedProblems
                    Line {
                        width: parent.width
                        when: full.shortDate(modelData.when)
                        text: modelData.text
                    }
                }

                // What is known about this device: its fixes, and what is
                // still open.
                Heading { text: "THIS DEVICE" }
                Column {
                    objectName: "deviceKnown"
                    width: parent.width
                    spacing: 8
                    Line {
                        visible: conversation.deviceFixes.length === 0 && conversation.deviceStillOpen.length === 0
                        text: "Nothing learned about this device yet. Its problems, and what fixed them, are kept here."
                    }
                    Repeater {
                        model: conversation.deviceFixes
                        DeviceFix {
                            objectName: "deviceFix-" + index
                            title: modelData.title
                            why: modelData.why
                            steps: modelData.steps
                        }
                    }
                    Repeater {
                        model: conversation.deviceStillOpen
                        OpenProblem {
                            objectName: "deviceOpen-" + index
                            text: modelData
                        }
                    }
                }
            }

            Line {
                visible: full.tab === 1
                width: parent.width
                text: conversation.aboutMe !== "" ? conversation.aboutMe
                    : "How you like to be helped, and who helps you. " + productName
                      + " writes this only after you say yes."
            }

            // Kept: every kept answer, newest first, with Forget where the
            // record's rows have Undo.
            Column {
                objectName: "keptList"
                visible: full.tab === 2
                width: parent.width
                spacing: 8
                Line {
                    visible: conversation.keptNotes.length === 0
                    width: parent.width
                    text: "Nothing is kept yet. Keep this, under an answer, files it here by topic."
                }
                Repeater {
                    model: conversation.keptNotes
                    Line {
                        objectName: "kept-" + index
                        width: parent.width
                        when: full.shortDate(Qt.formatDate(modelData.date, "d MMMM yyyy"))
                        text: modelData.title
                        status: modelData.topic
                        action: "Forget"
                        onActed: conversation.forget(modelData.path)
                    }
                }
            }
        }
    }

    Component { id: chooseStep; AssistantList {} }
    Component { id: startingStep; StartingPanel {} }
    Component { id: signInStep; SignInPanel {} }
    Component { id: problemStep; ProblemPanel {} }
    Component { id: doItStep; DoItCard {} }
}
