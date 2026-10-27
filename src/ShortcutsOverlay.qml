import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: overlay
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius
    color: Qt.rgba(0, 0, 0, 0.6)

    signal closeRequested()

    readonly property var shortcuts: [
        ["Ctrl+K", qsTr("Search contacts")],
        ["Alt+↑ / Alt+↓", qsTr("Previous or next conversation")],
        ["Enter", qsTr("Send the message")],
        ["Shift+Enter", qsTr("New line in the message")],
        ["Ctrl+O", qsTr("Send a file")],
        ["Ctrl+N", qsTr("Add a contact")],
        ["Ctrl+W", qsTr("Close the conversation")],
        ["F1", qsTr("Show or hide these shortcuts")],
        ["Ctrl+Q", qsTr("Quit")],
    ]

    MouseArea {
        anchors.fill: parent
        onClicked: overlay.closeRequested()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(520, parent.width - 32)
        height: column.implicitHeight + 48
        radius: theme.radius
        color: win.panelColor
        border.color: win.lineColor

        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: 24
            spacing: 16

            Label {
                text: qsTr("Keyboard shortcuts")
                color: win.textColor
                font.pixelSize: 18
                font.weight: Font.DemiBold
            }

            GridLayout {
                id: grid
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 24
                rowSpacing: 10

                Repeater {
                    model: overlay.shortcuts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        text: modelData[0]
                        color: theme.accent
                        font.family: "monospace"
                        Layout.row: index
                        Layout.column: 0
                    }
                }
                Repeater {
                    model: overlay.shortcuts
                    delegate: Label {
                        required property var modelData
                        required property int index
                        text: modelData[1]
                        color: win.textColor
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        Layout.row: index
                        Layout.column: 1
                    }
                }
            }
        }
    }
}
