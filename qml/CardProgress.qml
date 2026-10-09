// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A card at work: the bar, what it is doing now, and how far it has got.
Column {
    id: progress
    property int done: 0
    property int total: 0
    property string now
    property color tone: theme.accent
    property alias barObjectName: bar.objectName
    width: parent ? parent.width : 0
    spacing: 10
    Rectangle {
        id: bar
        width: parent.width
        height: 8; radius: 4
        color: theme.chip
        Rectangle {
            id: fill
            height: parent.height; radius: 4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: progress.tone }
                GradientStop { position: 1; color: Qt.lighter(progress.tone, theme.dark ? 1.15 : 1.35) }
            }
            width: progress.total > 0 ? Math.max(height, parent.width * Math.min(1, progress.done / progress.total))
                                      : parent.width * 0.28
            Behavior on width { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
            // Without a count, a stretch of colour moves along the bar.
            SequentialAnimation on x {
                running: progress.total <= 0 && progress.visible
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: bar.width - fill.width; duration: 1100; easing.type: Easing.InOutSine }
                NumberAnimation { from: bar.width - fill.width; to: 0; duration: 1100; easing.type: Easing.InOutSine }
            }
            onVisibleChanged: if (progress.total > 0) x = 0
        }
    }
    Row {
        width: parent.width
        Text {
            width: parent.width - countText.width
            elide: Text.ElideRight
            text: progress.now
            color: theme.text
            font.pixelSize: 15
        }
        Text {
            id: countText
            visible: progress.total > 0
            text: progress.done + " of " + progress.total
            color: theme.quiet
            font.pixelSize: 14
        }
    }
}
