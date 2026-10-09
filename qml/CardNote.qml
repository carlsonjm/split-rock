// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

// A line that needs reading: a warning, a reason a card can't go on, or a
// notice with a link to it.
Row {
    id: note
    property string text
    property color tone: theme.warn
    property string link
    signal linkOpened(string url)
    width: parent ? parent.width : 0
    spacing: 12
    Rectangle {
        y: 7
        width: 8; height: 8; radius: 4
        color: note.tone
    }
    Text {
        width: parent.width - 20
        wrapMode: Text.WordWrap
        text: note.link !== "" ? note.text + "  <a href=\"" + note.link + "\">Read it</a>" : note.text
        textFormat: note.link !== "" ? Text.StyledText : Text.PlainText
        linkColor: theme.accent
        color: theme.text
        font.pixelSize: 15
        lineHeight: 1.35
        onLinkActivated: url => note.linkOpened(url)
    }
}
