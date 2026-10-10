// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import "Icons.js" as Icons

// The upkeep cards, each a tile in its own colour: tapped, its card opens in
// the conversation. Laid out as a bento: as many columns as fit, chosen so the
// rows come out even, every tile in a row the same height, and a last row
// that comes out short stretched across the width.
Flow {
    id: shelf
    // Asked for when a tile opens its card, as the notebook folds away on a
    // narrow window.
    signal opened(string name)
    spacing: 10
    // The narrowest a tile is drawn.
    property real narrowest: 156
    readonly property int count: chores.length
    readonly property int columns: {
        const fit = Math.max(1, Math.floor((width + spacing) / (narrowest + spacing)))
        // The most columns that fit and leave no row short; otherwise as
        // many as fit.
        for (let n = Math.min(fit, count); n > 1; --n) {
            if (count % n === 0)
                return n
        }
        return Math.min(fit, count)
    }
    // The tallest tile's own height, which every tile takes.
    property real tallest: 96

    function tileWidth(index) {
        const inLastRow = count % columns
        const n = inLastRow !== 0 && index >= count - inLastRow ? inLastRow : columns
        return (width - (n - 1) * spacing) / n
    }

    function measure() {
        let most = 96
        for (let i = 0; i < tiles.count; ++i) {
            const tile = tiles.itemAt(i)
            if (tile)
                most = Math.max(most, tile.natural)
        }
        tallest = most
    }
    onWidthChanged: Qt.callLater(measure)

    readonly property var health: conversation.health
    readonly property var chores: [
        { name: "update", title: "Check for updates", sub: "Everything, with a restore point first" },
        { name: "space", title: "Free up space", sub: "Old copies and leftovers" },
        { name: "install", title: "Install an app", sub: "Say what you need, or remove one" },
        { name: "health", title: "Health check",
          sub: shelf.health.lastChecked === "" ? "A look over the computer"
             : shelf.health.attention === 0 ? "All good, " + shelf.health.lastChecked.toLowerCase()
             : shelf.health.attention === 1 ? "1 thing to look at" : shelf.health.attention + " things to look at" },
        // Restore points open from What changed's Go back, and from the
        // health check, so the shelf keeps six tiles.
        { name: "changes", title: "What changed", sub: "Undo, or go back to before" },
        { name: "community", title: "Community apps", sub: "Read, then update, AUR apps" }
    ]

    Repeater {
        id: tiles
        model: shelf.chores
        Rectangle {
            id: tile
            objectName: modelData.name === "update" ? "checkUpdates" : "upkeep-" + modelData.name
            readonly property color tone: theme.hue(theme.choreHue[modelData.name])
            readonly property bool open: conversation.chore === modelData.name
            readonly property bool attention: modelData.name === "health" && shelf.health.attention > 0
            readonly property real natural: tileColumn.implicitHeight + 28
            onNaturalChanged: Qt.callLater(shelf.measure)
            width: shelf.tileWidth(index)
            height: shelf.tallest
            radius: 18
            enabled: !conversation.choreBusy || open
            opacity: enabled ? 1 : 0.45
            color: theme.raised
            border.width: 1
            border.color: open ? tile.tone : theme.hairline
            Accessible.role: Accessible.Button
            Accessible.name: modelData.title
            Accessible.description: modelData.sub
            activeFocusOnTab: true
            Keys.onReturnPressed: tileArea.clicked(null)
            Keys.onSpacePressed: tileArea.clicked(null)

            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: parent.radius - 1
                gradient: Gradient {
                    GradientStop { position: 0; color: theme.soft(tile.tone, tileArea.pressed ? 0.30 : tileArea.containsMouse ? 0.22 : (theme.dark ? 0.15 : 0.10)) }
                    GradientStop { position: 1; color: theme.soft(tile.tone, 0) }
                }
            }

            Column {
                id: tileColumn
                x: 14; y: 14
                width: parent.width - 28
                spacing: 8
                Item {
                    width: parent.width
                    height: 36
                    Rectangle {
                        width: 36; height: 36; radius: 11
                        color: theme.soft(tile.tone, theme.dark ? 0.26 : 0.18)
                        Icon {
                            anchors.centerIn: parent
                            width: 19; height: 19
                            stroke: 2
                            path: Icons.forChore(modelData.name)
                            color: tile.tone
                        }
                    }
                    Rectangle {
                        visible: tile.attention
                        anchors.right: parent.right
                        anchors.top: parent.top
                        width: 10; height: 10; radius: 5
                        color: theme.warn
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: modelData.title
                    color: theme.text
                    font.pixelSize: 15
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: modelData.sub
                    color: tile.attention ? theme.warn : theme.quiet
                    font.pixelSize: 12
                    font.weight: tile.attention ? Font.DemiBold : Font.Normal
                    lineHeight: 1.2
                }
            }

            MouseArea {
                id: tileArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (conversation.openChore(modelData.name))
                        shelf.opened(modelData.name)
                }
            }
        }
    }
}
