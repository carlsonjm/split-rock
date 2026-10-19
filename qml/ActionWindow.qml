// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window

// One upkeep card on its own, as Core opens it (docs/CORE.md): an ordinary
// window, so a card workspace places it like any other, with the card inside
// and no helpers. It closes itself when the card does, and the card
// can't be closed while it is busy.
Window {
    id: actionWindow
    objectName: "actionWindow"
    title: {
        switch (helpers.chore) {
        case "update": return "Update"
        case "space": return "Free up space"
        case "install": return helpers.install.removing ? "Remove an app" : "Install a new app"
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
        switch (helpers.chore) {
        case "update": return helpers.update
        case "space": return helpers.space
        case "install": return helpers.install
        case "restore": return helpers.restore
        case "health": return helpers.health
        case "community": return helpers.community
        case "changes": return helpers.recent
        }
        return null
    }

    // The card closed, from its own Close or Not now. One card handing over
    // to another, as Health check does to Free up space, closes the first
    // before the second opens, so the window waits a moment and stays open
    // for the card that follows.
    Connections {
        target: helpers
        function onChoreChanged() {
            if (helpers.chore === "")
                Qt.callLater(actionWindow.closeIfEmpty)
        }
    }

    function closeIfEmpty() {
        if (helpers.chore === "")
            actionWindow.visible = false
    }

    onClosing: close => {
        if (helpers.choreBusy) {
            close.accepted = false
            return
        }
        if (actionWindow.job)
            actionWindow.job.close()
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentHeight: body.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        WheelScroll { flickable: scroller }

        Loader {
            id: body
            width: scroller.width
            sourceComponent: {
                switch (helpers.chore) {
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
