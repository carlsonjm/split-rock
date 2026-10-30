// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A card still looking: soft bars, pulsing, where its list will be. Each bar
// is that share of the card's width.
Column {
    id: looking
    property var bars: [0.9, 0.7, 0.8, 0.5]
    spacing: 10
    Repeater {
        model: looking.bars
        Rectangle {
            width: looking.width * modelData
            height: 12; radius: 6
            color: theme.chip
            SequentialAnimation on opacity {
                loops: Animation.Infinite
                NumberAnimation { from: 0.5; to: 1; duration: 700; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1; to: 0.5; duration: 700; easing.type: Easing.InOutSine }
            }
        }
    }
}
