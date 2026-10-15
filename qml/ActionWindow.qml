// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window

// One upkeep card on its own, as Core opens it (docs/CORE.md): an ordinary
// window, so a card workspace places it like any other, with the card inside
// and no conversation. It closes itself when the card does, and the card
// can't be closed while it is busy.
Window {
    id: actionWindow
    objectName: "actionWindow"
    title: {
        switch (conversation.chore) {
        case "update": return "Update"
        case "space": return "Free up space"
        case "install": return "Install a new app"
        case "restore": return "Restore points"
        case "health": return "Health check"
        case "community": return "Community apps"
        case "changes": return "What changed"
        }
        return productTitle
    }
    color: theme.sheet
    width: 560
    height: 680
    minimumWidth: 360
    minimumHeight: 360
    visible: false

    Theme { id: theme }

    readonly property var job: {
        switch (conversation.chore) {
        case "update": return conversation.update
        case "space": return conversation.space
        case "install": return conversation.install
        case "restore": return conversation.restore
        case "health": return conversation.health
        case "community": return conversation.community
        case "changes": return conversation.recent
        }
        return null
    }

    // The card closed, from its own Close or Not now.
    Connections {
        target: conversation
        function onChoreChanged() {
            if (conversation.chore === "")
                actionWindow.visible = false
        }
    }

    onClosing: close => {
        if (conversation.choreBusy) {
            close.accepted = false
            return
        }
        if (actionWindow.job)
            actionWindow.job.close()
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentHeight: body.height + 36
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        WheelScroll { flickable: scroller }

        Loader {
            id: body
            x: 18; y: 18
            width: scroller.width - 36
            sourceComponent: {
                switch (conversation.chore) {
                case "update": return updateCard
                case "space": return spaceCard
                case "install": return installCard
                case "restore": return restoreCard
                case "health": return healthCard
                case "community": return communityCard
                case "changes": return changesCard
                }
                return null
            }
        }
    }

    Component { id: updateCard; UpdateCard {} }
    Component { id: spaceCard; SpaceCard {} }
    Component { id: installCard; InstallCard {} }
    Component { id: restoreCard; RestoreCard {} }
    Component { id: healthCard; HealthCard {} }
    Component { id: communityCard; CommunityCard {} }
    Component { id: changesCard; ChangesCard {} }
}
