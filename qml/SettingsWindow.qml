// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window

// Split Rock Settings (docs/ASK.md, Split Rock Settings): the simplest page,
// on any Plasma desktop, with or without Shuffle. Ask on or off, who answers,
// the model, and Codex's sign-in, each a row on the inset fill at paper 10,
// 64 tall, 12 by 16 inside, its title at 15 and what it means at 13; a group
// named at 13 above its rows, the groups a section, 20, apart.
Window {
    id: settingsWindow
    objectName: "settingsWindow"
    title: "Split Rock Settings"
    color: theme.sheet
    width: 560
    height: 640
    minimumWidth: 360
    minimumHeight: 420
    visible: false

    Theme { id: theme }

    readonly property bool ready: askSettings.state === "ready"
    readonly property bool downloading: askSettings.state === "downloading" || askSettings.state === "verifying"
    readonly property string modelName: askSettings.name.length > 0 ? askSettings.name : "The model"

    onVisibleChanged: if (visible) askSettings.refresh()

    // A row: its words, and a control at its end.
    component SettingRow: Rectangle {
        id: row
        property string title
        property string detail
        property string rowName
        default property alias trailing: end.data
        signal tapped()
        objectName: rowName
        width: parent ? parent.width : 0
        height: Math.max(64, words.implicitHeight + 24)
        radius: theme.cardRadius
        color: theme.inset
        Column {
            id: words
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 32 - (end.childrenRect.width > 0 ? end.childrenRect.width + 12 : 0)
            spacing: 2
            Text {
                width: parent.width
                text: row.title
                color: theme.text
                font.pixelSize: 15
                wrapMode: Text.Wrap
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: row.detail
                color: theme.quiet
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }
        }
        Item {
            id: end
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            width: childrenRect.width
            height: childrenRect.height
        }
        MouseArea {
            anchors.fill: parent
            z: -1
            onClicked: row.tapped()
        }
    }

    // A group's name, above its rows.
    component Group: Text {
        color: theme.quiet
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    // Who answers: a ring, filled when chosen.
    component Choice: Rectangle {
        property bool chosen: false
        width: 22; height: 22; radius: 11
        color: "transparent"
        border.width: chosen ? 7 : 2
        border.color: chosen ? theme.accent : theme.edge
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentHeight: page.implicitHeight + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        WheelScroll { flickable: scroller }

        Column {
            id: page
            x: 24; y: 24
            width: scroller.width - 48
            spacing: 20

            Column {
                width: parent.width
                spacing: 4
                Text {
                    text: "Ask"
                    color: theme.text
                    font.pixelSize: 24
                    font.weight: Font.DemiBold
                }
                Text {
                    width: parent.width
                    text: askSettings.installed ? "Answers questions about this computer, in Search's Ask tab."
                        : "Ask isn't on this computer. It answers here once Split Rock's Ask is installed."
                    color: theme.quiet
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
            }

            SettingRow {
                rowName: "settings-ask"
                visible: askSettings.installed
                title: "Ask in Search"
                detail: askSettings.enabled ? "Answers questions in Search's Ask tab." : "Off. Search shows the Helpers alone."
                onTapped: askSettings.enabled = !askSettings.enabled
                Rectangle {
                    objectName: "settings-ask-switch"
                    width: 44; height: 24; radius: 12
                    color: askSettings.enabled ? theme.accent : theme.chip
                    Accessible.role: Accessible.CheckBox
                    Accessible.name: "Ask in Search"
                    Accessible.checkable: true
                    Accessible.checked: askSettings.enabled
                    Rectangle {
                        x: askSettings.enabled ? parent.width - width - 3 : 3
                        y: 3
                        width: 18; height: 18; radius: 9
                        color: askSettings.enabled ? theme.accentInk : theme.quiet
                        Behavior on x { NumberAnimation { duration: 140; easing.type: Easing.OutCubic } }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 8
                visible: askSettings.installed
                Group { text: "Who answers" }
                Column {
                    width: parent.width
                    spacing: 4
                    SettingRow {
                        rowName: "settings-hybrid"
                        title: settingsWindow.modelName + ", with Codex for bigger fixes"
                        detail: "The default. " + settingsWindow.modelName + " answers here, private and offline once it's downloaded. A bigger fix goes to Codex only when you tap Ask Codex."
                        opacity: askSettings.enabled ? 1 : 0.5
                        onTapped: if (askSettings.enabled) askSettings.setCodexOnly(false)
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: title
                        Accessible.checked: !askSettings.codexOnly
                        Choice { chosen: !askSettings.codexOnly }
                    }
                    SettingRow {
                        rowName: "settings-codex-only"
                        title: "Codex only"
                        detail: askSettings.codexInstalled
                            ? "No download. Questions go to Codex online when you tap, on your ChatGPT plan. Warnings about scams and passwords still come from this computer."
                            : "Needs Codex, which isn't installed on this computer."
                        opacity: askSettings.enabled && (askSettings.codexInstalled || askSettings.codexOnly) ? 1 : 0.5
                        onTapped: if (askSettings.enabled && askSettings.codexInstalled) askSettings.setCodexOnly(true)
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: title
                        Accessible.checked: askSettings.codexOnly
                        Choice { chosen: askSettings.codexOnly }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 8
                visible: askSettings.installed
                Group { text: "The model on this computer" }
                SettingRow {
                    rowName: "settings-model"
                    title: settingsWindow.modelName
                    detail: !askSettings.engine ? "llama.cpp, which runs it, isn't installed. Install it first."
                        : settingsWindow.ready ? askSettings.size + " · downloaded"
                        : settingsWindow.downloading ? Math.round(100 * askSettings.progress) + "% of " + askSettings.size
                        : askSettings.state === "failed" && askSettings.problem.length > 0 ? askSettings.problem
                        : askSettings.size.length > 0 ? askSettings.size + " · not downloaded" : "Not downloaded"
                    Pill {
                        objectName: "settings-model-action"
                        visible: askSettings.engine || settingsWindow.ready
                        text: settingsWindow.ready ? "Remove" : settingsWindow.downloading ? "Stop" : "Download"
                        onClicked: settingsWindow.ready ? askSettings.remove()
                            : settingsWindow.downloading ? askSettings.cancelDownload() : askSettings.download()
                    }
                }
                Rectangle {
                    objectName: "settings-model-progress"
                    visible: settingsWindow.downloading
                    width: parent.width
                    height: 4
                    radius: 2
                    color: theme.chip
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, askSettings.progress))
                        height: parent.height
                        radius: 2
                        color: theme.accent
                    }
                }
                Text {
                    width: parent.width
                    leftPadding: 16
                    rightPadding: 16
                    visible: askSettings.credit.length > 0
                    text: askSettings.credit
                    color: theme.quiet
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }
            }

            Column {
                width: parent.width
                spacing: 8
                visible: askSettings.installed
                Group { text: "Codex" }
                SettingRow {
                    rowName: "settings-codex"
                    title: !askSettings.codexInstalled ? "Codex isn't installed"
                        : askSettings.codexSignedIn ? "Signed in with ChatGPT" : "Sign in with ChatGPT"
                    detail: !askSettings.codexInstalled
                        ? (askSettings.codexProblem.length > 0 ? askSettings.codexProblem : "Codex isn't installed on this computer.")
                        : askSettings.codexSignedIn ? "Your plan pays for what Codex does. Split Rock never sees your account or bill."
                        : "Codex signs you in through its own page, and your ChatGPT plan pays for what it does. Split Rock never sees your password, your account or your bill."
                    Pill {
                        objectName: "settings-codex-action"
                        visible: askSettings.codexInstalled
                        text: askSettings.codexSignedIn ? "Sign out" : "Sign in"
                        onClicked: askSettings.codexSignedIn ? askSettings.signOutOfCodex() : askSettings.signInToCodex()
                    }
                }
            }
        }
    }
}
