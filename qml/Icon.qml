// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A line icon drawn from SVG path data on a 24-unit grid, as in the mock-up.
Canvas {
    id: icon
    property string path
    property color color: "black"
    property real stroke: 1.75
    implicitWidth: 16
    implicitHeight: 16
    onPathChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onStrokeChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.scale(width / 24, height / 24)
        ctx.lineWidth = stroke
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        ctx.strokeStyle = icon.color
        ctx.path = icon.path
        ctx.stroke()
    }
}
