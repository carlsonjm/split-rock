// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window

// The full window Expand opens: an ordinary window, so that Kadunce makes it
// the Active card and the desktop's own title bar sits above it, with the full
// view inside. The card it takes over from fades once it is shown.
Window {
    id: fullWindow
    title: productTitle
    color: theme.sheet
    width: 1100
    height: 720
    minimumWidth: 640
    minimumHeight: 420
    visible: false

    Theme { id: theme }

    onActiveChanged: if (active) view.focusField()

    Workspace {
        id: view
        objectName: "fullView"
        anchors.fill: parent
        anchors.margins: 18
    }
}
