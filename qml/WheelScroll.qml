// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A mouse wheel scrolls the given Flickable a steady step: one notch moves 80
// pixels at Plasma's usual three lines per scroll, eased, the same distance
// whatever the mouse reports per notch. A Flickable's own wheel step
// is shorter, and shorter again for mice that report a notch in parts. A
// touchpad's gesture, which carries its own pixel distances and momentum,
// stays with the Flickable.
WheelHandler {
    id: handler

    required property Flickable flickable
    readonly property real step: Qt.styleHints.wheelScrollLines * 80 / 3

    target: null
    acceptedDevices: PointerDevice.Mouse
    orientation: Qt.Vertical

    onWheel: event => {
        const flick = handler.flickable;
        const top = flick.originY - flick.topMargin;
        const bottom = Math.max(top, flick.originY + flick.contentHeight
            + flick.bottomMargin - flick.height);
        const from = glide.running ? glide.to : flick.contentY;
        const to = Math.max(top, Math.min(bottom,
            from - event.angleDelta.y / 120 * handler.step));
        if (Math.abs(to - from) < 0.5)
            return;
        flick.cancelFlick();
        glide.stop();
        glide.from = flick.contentY;
        glide.to = to;
        glide.duration = Math.max(50, Math.min(200,
            Math.abs(to - flick.contentY) * 200 / handler.step));
        glide.start();
    }

    property NumberAnimation glide: NumberAnimation {
        target: handler.flickable
        property: "contentY"
        easing.type: Easing.OutCubic
    }
}
