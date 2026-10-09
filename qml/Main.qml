// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import "Icons.js" as Icons

// Genie's card, in the shape of Shuffle's Search: the question in its header
// and the answer below. In Spread it holds the centre, where Kadunce draws it;
// otherwise it stands centred over the work. Expand grows the card, then the
// full window takes over from it. With the on-screen keys up the card rises
// only as far as it must to stay above them, then shortens. Esc, or a tap
// beside the card, closes it. The shell places it (src/desktop/Shell.h).
Window {
    id: window
    title: productTitle
    color: "transparent"
    flags: Qt.FramelessWindowHint
    visible: false

    Theme { id: theme }

    // The card has faded, closed or handed over to the full window.
    signal finished()

    property bool shown: false
    readonly property bool expanded: shell.expanded
    readonly property bool guest: shell.mode === "guest"
    // The room the panels leave, in this window.
    readonly property rect area: shell.workArea.width > 0 ? shell.workArea : Qt.rect(0, 0, width, height)
    // A way to sign in, chosen in the card: it opens in the full window, so
    // the browser it brings can come in front.
    property string pendingSignIn: ""
    // Where the on-screen keys lie over this window, as the input method
    // reports them; empty while they are down. The keys hold no room of their
    // own, so the card keeps above them itself.
    property rect keysRect: Qt.inputMethod.visible ? Qt.inputMethod.keyboardRectangle : Qt.rect(0, 0, 0, 0)
    readonly property bool keysUp: keysRect.width > 0 && keysRect.height > 0 && keysRect.y < height
    // How far up from the window's bottom the card keeps clear.
    property real keysReach: keysUp ? height - keysRect.y + 10 : 0
    Behavior on keysReach {
        NumberAnimation { id: keysMotion; duration: 220; easing.type: Easing.OutCubic }
    }

    readonly property bool canAsk: conversation.phase === "ready"
    readonly property bool working: conversation.phase === "working"
    // The Do it card has the answer's place to itself, as in the mock-up.
    readonly property bool cardMode: !conversation.choosing && (conversation.cardOpen || conversation.chore !== "")
                                    && (canAsk || working || conversation.chore !== "")

    function open() {
        shown = true
        field.forceActiveFocus()
    }

    function finish() {
        if (!shown)
            return
        shown = false
        Qt.inputMethod.hide()
        finishing.restart()
    }

    function expand() {
        shell.expand()
        Qt.inputMethod.hide()
    }

    function signInThroughFull(methodId) {
        pendingSignIn = methodId
        expand()
    }

    Connections {
        target: shell
        function onHandedOver() {
            if (window.pendingSignIn !== "") {
                conversation.signIn(window.pendingSignIn)
                window.pendingSignIn = ""
            }
        }
    }

    function send() {
        if (working) {
            conversation.stop()
        } else if (field.text === conversation.question && field.text !== "") {
            // The question already answered: clear it, and start over.
            field.text = ""
            conversation.newQuestion()
            field.forceActiveFocus()
        } else if (canAsk && field.text.trim() !== "") {
            conversation.ask(field.text)
        }
    }

    Timer {
        id: finishing
        interval: 180
        onTriggered: window.finished()
    }

    Shortcut {
        sequence: "Esc"
        onActivated: window.finish()
    }

    onCardModeChanged: if (!cardMode && !expanded) field.forceActiveFocus()

    // The field shows the question being answered, however it was asked.
    Connections {
        target: conversation
        function onAnswerChanged() {
            if (conversation.question !== "" && conversation.question !== "Show me how instead" && conversation.question !== field.text)
                field.text = conversation.question
        }
    }

    // The work around the card: a tap there closes it. In Spread a tap beside
    // the card is Kadunce's, which closes it the same way.
    Item {
        id: around
        anchors.fill: parent
        TapHandler {
            onTapped: event => {
                const point = card.mapFromItem(around, event.position.x, event.position.y)
                if (!card.contains(point))
                    window.finish()
            }
        }
    }

    Rectangle {
        id: card
        objectName: "card"

        // As a guest the card is where Kadunce puts it, and grows to where
        // Kadunce says; otherwise 64% of the work, growing to all of it less a
        // 10 px gutter.
        readonly property rect guestPlace: window.expanded ? shell.guestActive : shell.guestCard
        readonly property real restHeight: window.guest ? guestPlace.height
            : window.expanded ? Math.max(1, window.area.height - 20) : Math.round(window.area.height * 0.64)
        readonly property real keysLine: window.height - window.keysReach
        // A guest never rises out of its place; it only shortens.
        readonly property real highest: window.guest ? guestPlace.y
            : window.area.y + Math.min(10, Math.round((window.area.height - restHeight) / 2))

        width: window.guest ? guestPlace.width
            : window.expanded ? Math.max(1, window.area.width - 20)
            : Math.min(window.area.width - 20, Math.max(Math.round(window.area.width * 0.64), 420))
        height: Math.max(Math.min(restHeight, 160), Math.min(restHeight, keysLine - highest))
        x: window.guest ? guestPlace.x : window.area.x + (window.area.width - width) / 2
        y: Math.max(highest, Math.min(keysLine - height, window.guest ? guestPlace.y
            : window.area.y + (window.expanded ? 10 : Math.round((window.area.height - height) / 2))))
        // Nothing moves until the card is shown: it opens at its place.
        Behavior on x { enabled: window.guest && window.shown; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on y { enabled: window.guest && window.shown && !keysMotion.running; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        Behavior on width { enabled: window.shown; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
        // The keys' own easing already moves the card; chasing it would lag.
        Behavior on height { enabled: window.shown && !keysMotion.running; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

        radius: 8
        color: theme.sheet
        border.width: 1
        border.color: theme.edge
        clip: true
        opacity: window.shown ? 1 : 0
        scale: window.shown ? 1 : 0.96
        Behavior on opacity { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        Item {
            id: content
            anchors.fill: parent
            anchors.margins: 22

            // The header: the name on the left, the question in the middle,
            // Expand on the right. At full size only the name stays.
            Item {
                id: header
                objectName: "header"
                width: parent.width
                height: 44

                Row {
                    id: nameRow
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Mark { anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: productName
                        color: theme.text
                        font.pixelSize: 15
                        font.weight: Font.ExtraBold
                    }
                }

                // The question field keeps to the middle: centred, and as
                // clear of the name as of Expand, as Search's is in its header.
                Rectangle {
                    id: fieldBox
                    readonly property real side: Math.max(nameRow.x + nameRow.width, header.width - expandButton.x) + 12
                    width: Math.max(160, Math.min(header.width - 2 * side, Math.max(360, header.width / 2)))
                    height: 44
                    x: (header.width - width) / 2
                    radius: height / 2
                    color: field.activeFocus || field.text !== "" ? theme.panel : "transparent"
                    border.width: 1
                    border.color: field.activeFocus && window.canAsk ? theme.accent : theme.edge
                    opacity: window.expanded ? 0 : (window.canAsk || window.working ? 1 : 0.5)
                    visible: opacity > 0
                    enabled: !window.expanded
                    Behavior on opacity { NumberAnimation { duration: 120 } }

                    TextInput {
                        id: field
                        objectName: "field"
                        anchors.left: parent.left
                        anchors.leftMargin: 18
                        anchors.right: sendButton.left
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        focus: true
                        clip: true
                        color: theme.text
                        selectionColor: theme.palette.highlight
                        selectedTextColor: theme.palette.highlightedText
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        selectByMouse: true
                        Accessible.role: Accessible.EditableText
                        Accessible.name: "Ask " + productName
                        onAccepted: window.send()

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            elide: Text.ElideRight
                            visible: field.text === ""
                            text: "Ask, or say what you want done"
                            color: theme.quiet
                            font: field.font
                        }
                    }

                    // Send; Stop while an answer comes; clear once it has.
                    Rectangle {
                        id: sendButton
                        objectName: "sendButton"
                        readonly property bool clears: !window.working && field.text !== "" && field.text === conversation.question
                        anchors.right: parent.right
                        anchors.rightMargin: 5
                        anchors.verticalCenter: parent.verticalCenter
                        width: 34; height: 34; radius: 17
                        color: clears ? theme.chip : theme.accent
                        opacity: window.working || clears || (window.canAsk && field.text.trim() !== "") ? 1 : 0.38
                        Accessible.role: Accessible.Button
                        Accessible.name: window.working ? "Stop" : clears ? "New question" : "Send"
                        Icon {
                            anchors.centerIn: parent
                            width: 15; height: 15
                            path: window.working ? Icons.stop : sendButton.clears ? Icons.close : Icons.arrow
                            color: sendButton.clears ? theme.text : theme.accentInk
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: window.send()
                        }
                    }
                }

                // Expand: the suite's grey pill, 30 high in its 42 touch.
                Item {
                    id: expandButton
                    objectName: "expandButton"
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: expandLabel.x + expandLabel.implicitWidth + 14
                    height: 42
                    opacity: window.expanded ? 0 : 1
                    visible: opacity > 0
                    enabled: !window.expanded
                    Behavior on opacity { NumberAnimation { duration: 120 } }
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: expandLabel.text
                    Accessible.onPressAction: window.expand()
                    Keys.onReturnPressed: window.expand()
                    Keys.onSpacePressed: window.expand()

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 30
                        radius: height / 2
                        color: expandArea.pressed ? theme.edge : expandArea.containsMouse ? theme.line : theme.chip
                        border.width: expandButton.activeFocus ? 2 : 0
                        border.color: theme.palette.highlight
                        Icon {
                            x: 10
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14; height: 14
                            path: Icons.expand
                            color: theme.text
                        }
                        Text {
                            id: expandLabel
                            x: 30
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Expand"
                            color: theme.text
                            font.pixelSize: 13
                            font.weight: Font.Bold
                        }
                    }
                    MouseArea {
                        id: expandArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.expand()
                    }
                }
            }

            // Below the header, the answer.
            Flickable {
                id: page
                y: header.height + 16
                width: parent.width
                height: parent.height - y
                contentWidth: width
                contentHeight: below.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                opacity: window.expanded ? 0 : 1
                visible: opacity > 0
                enabled: !window.expanded
                Behavior on opacity { NumberAnimation { duration: 120 } }

                Column {
                    id: below
                    width: page.width
                    spacing: 14

                    // Questions to start from.
                    Flow {
                        width: parent.width
                        spacing: 8
                        visible: !conversation.choosing && !window.cardMode && (conversation.phase === "ready" || conversation.phase === "working")
                        Repeater {
                            model: conversation.suggestions
                            Pill {
                                implicitHeight: 40
                                padding: 16
                                readonly property bool picked: conversation.question === modelData
                                filled: picked
                                enabled: window.canAsk
                                text: modelData
                                onClicked: {
                                    field.text = modelData
                                    conversation.ask(modelData)
                                }
                            }
                        }
                    }

                    Loader {
                        id: body
                        width: parent.width
                        sourceComponent: {
                            if (conversation.choosing)
                                return assistantList
                            // An upkeep card needs no assistant, so it comes
                            // before the assistant's own steps.
                            switch (conversation.chore) {
                            case "update": return updateCard
                            case "space": return spaceCard
                            case "install": return installCard
                            case "restore": return restoreCard
                            case "health": return healthCard
                            case "community": return communityCard
                            }
                            switch (conversation.phase) {
                            case "starting": return starting
                            case "signIn":
                            case "signingIn": return signInPanel
                            case "problem": return problemPanel
                            default: return conversation.cardOpen ? doItCard : answerCard
                            }
                        }
                    }

                    Text {
                        objectName: "notice"
                        visible: conversation.notice !== "" && !conversation.choosing
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: conversation.notice
                        color: theme.text
                        font.pixelSize: 14
                        lineHeight: 1.3
                    }

                    // Which assistant is answering, and the way to another.
                    Item {
                        width: parent.width
                        height: assistantPill.height
                        visible: conversation.assistantId !== "" && !conversation.choosing
                        Pill {
                            id: assistantPill
                            objectName: "assistantPill"
                            anchors.right: parent.right
                            implicitHeight: 40
                            padding: 14
                            fontSize: 13
                            enabled: conversation.phase !== "working" && conversation.phase !== "signingIn"
                            text: conversation.assistantName + "  ▾"
                            onClicked: conversation.showAssistants()
                        }
                    }
                }
            }
        }
    }

    Component { id: assistantList; AssistantList {} }
    Component { id: answerCard; AnswerCard {} }
    Component { id: doItCard; DoItCard {} }
    Component { id: updateCard; UpdateCard {} }
    Component { id: spaceCard; SpaceCard {} }
    Component { id: installCard; InstallCard {} }
    Component { id: restoreCard; RestoreCard {} }
    Component { id: healthCard; HealthCard {} }
    Component { id: communityCard; CommunityCard {} }
    Component { id: signInPanel; SignInPanel { signInWith: id => window.signInThroughFull(id) } }
    Component { id: starting; StartingPanel {} }
    Component { id: problemPanel; ProblemPanel {} }
}
