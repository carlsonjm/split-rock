// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The Community apps card: the apps from the community shelf with a newer
// recipe, and for each, what changed in its recipe before one tap updates it
// (src/app/CommunityApps.h).
CardFrame {
    id: card
    objectName: "communityCard"
    readonly property var job: conversation.community
    readonly property string stage: card.job.stage
    readonly property var apps: card.job.apps
    readonly property var review: card.job.review
    readonly property bool risky: (card.review.flags || []).some(f => f.tone === "bad")
    hue: card.stage === "failed" ? "coral" : card.stage === "done" ? "green" : "orange"

    CardHead {
        headingObjectName: "communityHeading"
        tone: card.tone
        icon: card.stage === "done" ? Icons.check : card.stage === "failed" ? Icons.alert : Icons.box
        spinning: false
        heading: {
            switch (card.stage) {
            case "looking": return "Looking at community apps"
            case "nothing": return "Community apps are up to date"
            case "reviewing": return "Reading " + card.review.name + "'s recipe"
            case "reviewed": return card.review.name
            case "updating": return "Updating " + card.review.name
            case "done": return card.review.name + " is up to date"
            case "failed": return card.review.name ? "Updating " + card.review.name + " didn't finish" : "Community apps"
            }
            return card.apps.length === 1 ? "1 community app can update" : card.apps.length + " community apps can update"
        }
        sub: {
            switch (card.stage) {
            case "looking": return "Apps from the community shelf (the AUR), against their newest recipes."
            case "nothing": return card.job.unlisted > 0 ? card.job.unlisted + " installed from elsewhere aren't on the shelf to compare." : ""
            case "reviewing": return "Fetching it, to show what changed since your version."
            case "reviewed": return card.review.installed + " → " + card.review.latest
                                   + (card.review.maintainer ? " · looked after by " + card.review.maintainer : "")
            case "updating": return "Building it from the recipe you saw. This can take a while; you can keep working."
            case "done": return "Now " + card.review.latest + "."
            case "failed": return card.job.why
            }
            return "Anyone can publish a recipe there, so each is read before it updates."
        }
    }

    Column {
        objectName: "communityApps"
        visible: card.stage === "ready"
        width: card.innerWidth
        spacing: 8
        Repeater {
            model: card.stage === "ready" ? card.apps : []
            CardRow {
                objectName: "community-" + index
                tone: card.tone
                icon: Icons.box
                title: modelData.name
                detail: modelData.installed + " → " + modelData.latest + (modelData.orphaned ? " · nobody looks after it" : "")
                trailing: "Read"
                opens: true
                Accessible.name: "Read what changed in " + modelData.name
                onClicked: card.job.look(modelData.name)
            }
        }
    }

    CardProgress {
        visible: card.stage === "reviewing" || card.stage === "updating"
        barObjectName: "communityProgress"
        tone: card.tone
        done: card.job.done
        total: card.job.total
        now: card.job.now
    }

    // What the recipe's changes say, plainly.
    Column {
        objectName: "communityFlags"
        visible: card.stage === "reviewed"
        width: card.innerWidth
        spacing: 10
        Repeater {
            model: card.stage === "reviewed" ? card.review.blockers : []
            CardNote { text: modelData; tone: theme.bad }
        }
        Repeater {
            model: card.stage === "reviewed" ? card.review.flags : []
            CardNote {
                text: modelData.text
                tone: modelData.tone === "good" ? theme.good : modelData.tone === "bad" ? theme.bad : theme.warn
            }
        }
    }

    Flow {
        visible: card.stage === "reviewed"
        width: card.innerWidth
        spacing: 18
        CardFact {
            icon: Icons.shield
            label: "Restore point first"
            tone: theme.good
        }
        CardFact {
            icon: Icons.lock
            label: "Needs your password"
        }
    }

    Fold {
        width: card.innerWidth
        title: card.stage === "reviewed" ? (card.review.firstLook ? "Read the whole recipe" : "Read what changed")
                                         : "Show what it did"
        body: card.stage === "reviewed" ? (card.review.diff || "")
            : card.stage === "done" || card.stage === "failed" ? card.job.log : ""
    }

    Flow {
        width: card.innerWidth
        spacing: 10
        visible: !card.job.busy
        Pill {
            objectName: "communityButton"
            visible: card.stage === "reviewed"
            implicitHeight: 52
            padding: 28
            fontSize: 16
            filled: true
            tone: card.risky ? theme.bad : card.tone
            toneInk: theme.hueInk
            enabled: (card.review.blockers || []).length === 0
            iconPath: Icons.refresh
            text: card.risky ? "Update anyway" : "Update " + card.review.name
            onClicked: card.job.start()
        }
        Pill {
            visible: card.stage === "reviewed" || card.stage === "done" || (card.stage === "failed" && card.apps.length > 0)
            implicitHeight: 52
            padding: 22
            fontSize: 15
            iconPath: Icons.back
            text: card.stage === "reviewed" ? "Back" : "The others"
            onClicked: card.job.back()
        }
        Pill {
            visible: card.stage === "failed" && card.apps.length === 0
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: "Try again"
            onClicked: card.job.check()
        }
        Pill {
            objectName: "communityClose"
            implicitHeight: 52
            padding: 22
            fontSize: 15
            text: card.stage === "ready" ? "Not now" : "Close"
            onClicked: card.job.close()
        }
    }
}
