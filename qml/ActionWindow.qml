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
    // The Helper's name, then its one fact after a dot, "Update · 12
    // waiting": Shuffle's Context Bar shows the piece after the dot beside
    // the name, dimmed, so the bar says what the Helper found without the
    // card being in front.
    title: fact === "" ? helperName : helperName + " · " + fact
    readonly property string helperName: {
        switch (helpers.chore) {
        case "update": return "Update"
        case "space": return "Free up space"
        case "install": return helpers.install.store ? "Add the app store" : helpers.install.yours ? "Your apps"
                             : helpers.install.removing ? "Remove an app" : "Install a new app"
        case "restore": return "Restore points"
        case "health": return "Health check"
        case "community": return "Community apps"
        case "changes": return "What changed"
        case "tidy": return "Tidy up Downloads"
        }
        return productTitle
    }
    readonly property string fact: {
        switch (helpers.chore) {
        case "update": return helpers.update.waiting > 0 ? helpers.update.waiting + " waiting" : ""
        case "space": return helpers.space.diskKb > 0 ? size(helpers.space.freeKb) + " free" : ""
        case "health": return helpers.health.attention > 0 ? helpers.health.attention + " to look at" : ""
        }
        return ""
    }

    // "1.2 GB", "340 MB", as the Free up space card writes them.
    function size(kb) {
        if (kb >= 1024 * 1024)
            return (kb / 1024 / 1024).toFixed(kb >= 10 * 1024 * 1024 ? 0 : 1) + " GB"
        return Math.round(kb / 1024) + " MB"
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
        case "tidy": return helpers.tidy
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
                case "tidy": return tidyCard
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
    Component { id: tidyCard; TidyCard {} }
}
