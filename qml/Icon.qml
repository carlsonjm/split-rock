// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// An icon drawn from SVG path data on a 24-unit grid: a line icon, as in the
// mock-up, or a filled shape such as the assistant's mark.
Canvas {
    id: icon
    property string path
    property color color: "black"
    property real stroke: 1.75
    property bool filled: false
    implicitWidth: 16
    implicitHeight: 16
    onPathChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onStrokeChanged: requestPaint()
    onFilledChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.scale(width / 24, height / 24)
        ctx.path = icon.path
        if (icon.filled) {
            ctx.fillStyle = icon.color
            ctx.fill()
            return
        }
        ctx.lineWidth = stroke
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.strokeStyle = icon.color
        ctx.stroke()
    }
}
