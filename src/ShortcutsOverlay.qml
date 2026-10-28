import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Every shortcut, grouped, in the layout of Hype's overlay.
Popup {
    id: overlay

    readonly property var ui: theme.colors

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 28
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.35) }
    background: Rectangle {
        color: Qt.alpha(overlay.ui.panel, 0.96)
        border.color: overlay.ui.accent
        border.width: 2
        radius: theme.radius
    }

    readonly property var groups: [
        { title: qsTr("Conversations"), keys: [
            ["Ctrl+K / Ctrl+T", qsTr("Search contacts")],
            ["Ctrl+Shift+K", qsTr("Conversations or all contacts")],
            ["Alt+↑ / Alt+↓", qsTr("Previous or next conversation")],
            ["Ctrl+W", qsTr("Close the conversation")],
            ["Ctrl+F", qsTr("Search this conversation")],
            ["Ctrl+N", qsTr("Add a contact")],
            ["Ctrl+J", qsTr("Join a group chat")] ] },
        { title: qsTr("Writing"), keys: [
            ["Enter", qsTr("Send the message")],
            ["Shift+Enter", qsTr("New line in the message")],
            ["↑", qsTr("Correct your last message")],
            ["Esc", qsTr("Stop correcting")],
            ["Shift+Click / Shift+Drag", qsTr("Select whole messages")],
            ["Ctrl+C", qsTr("Copy the selected messages")] ] },
        { title: qsTr("Files"), keys: [
            ["Ctrl+O", qsTr("Send a file through the server")],
            ["Ctrl+Shift+O", qsTr("Send a file directly")] ] },
        { title: qsTr("Window"), keys: [
            ["? / F1", qsTr("Show these shortcuts")],
            ["Ctrl+Q", qsTr("Quit")] ] }
    ]

    contentItem: ColumnLayout {
        spacing: 22
        focus: true
        // Window shortcuts stand down while a modal popup is open, so close from here.
        Keys.onPressed: event => {
            if (event.text === "?" || event.key === Qt.Key_F1) {
                overlay.close()
                event.accepted = true
            }
        }

        Label {
            text: qsTr("Keyboard shortcuts")
            font.pixelSize: 16
            font.bold: true
            color: overlay.ui.foreground
        }
        GridLayout {
            columns: win.width < 820 ? 1 : 2
            columnSpacing: 48
            rowSpacing: 22

            Repeater {
                model: overlay.groups
                ColumnLayout {
                    required property var modelData
                    Layout.alignment: Qt.AlignTop
                    spacing: 7
                    Label {
                        Layout.bottomMargin: 2
                        text: modelData.title.toUpperCase()
                        font.pixelSize: 11
                        font.letterSpacing: 1
                        color: overlay.ui.accent
                    }
                    Repeater {
                        model: modelData.keys
                        RowLayout {
                            required property var modelData
                            spacing: 14
                            Label {
                                Layout.preferredWidth: 130
                                text: modelData[0].replace(/\+/g, " + ")
                                font.family: "monospace"
                                font.pixelSize: 12
                                color: overlay.ui.foreground
                            }
                            Label {
                                text: modelData[1]
                                font.pixelSize: 13
                                color: overlay.ui.muted
                            }
                        }
                    }
                }
            }
        }
    }
}
