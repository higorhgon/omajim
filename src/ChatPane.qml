import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: pane
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    function send() {
        if (backend.sendMessage(composer.text))
            composer.clear()
    }

    Connections {
        target: backend
        function onCurrentChatChanged() {
            if (backend.currentChat !== "")
                composer.forceActiveFocus()
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: backend.currentChat === ""
        spacing: 6
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Pick a conversation")
            color: win.textColor
            font.pixelSize: 18
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Ctrl+K searches your contacts. F1 lists every shortcut.")
            color: win.mutedColor
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: backend.currentChat !== ""
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 10
            PresenceDot { presence: backend.currentChatPresence }
            Label {
                text: backend.currentChatName
                color: win.textColor
                font.pixelSize: 17
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.maximumWidth: pane.width * 0.45
            }
            Label {
                Layout.fillWidth: true
                text: backend.currentChatStatusText !== ""
                      ? "%1 — %2".arg(win.presenceLabel(backend.currentChatPresence)).arg(backend.currentChatStatusText)
                      : win.presenceLabel(backend.currentChatPresence)
                color: win.mutedColor
                elide: Text.ElideRight
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: win.lineColor
        }

        ListView {
            id: messages
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            topMargin: 12
            bottomMargin: 12
            model: backend.messages
            ScrollBar.vertical: ScrollBar {}
            delegate: ChatLine {}

            onCountChanged: Qt.callLater(positionViewAtEnd)
            Connections {
                target: backend
                function onCurrentChatChanged() { Qt.callLater(messages.positionViewAtEnd) }
            }

            Label {
                anchors.centerIn: parent
                visible: messages.count === 0
                text: qsTr("No messages with %1 yet.").arg(backend.currentChatName)
                color: win.mutedColor
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.topMargin: 0
            implicitHeight: Math.min(160, composerRow.implicitHeight + 12)
            radius: theme.radius
            color: win.surfaceColor
            border.color: composer.activeFocus ? theme.accent : win.lineColor

            RowLayout {
                id: composerRow
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                ToolButton {
                    Material.roundedScale: theme.radius
                    Layout.alignment: Qt.AlignBottom
                    text: "+"
                    font.pixelSize: 20
                    onClicked: backend.chooseFileToSend()
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Send a file (Ctrl+O)")
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 40
                    TextArea {
                        id: composer
                        wrapMode: TextEdit.Wrap
                        background: null
                        color: win.textColor
                        // Enter sends; Shift+Enter starts a new line.
                        Keys.onReturnPressed: event => {
                            if (event.modifiers & Qt.ShiftModifier)
                                event.accepted = false
                            else
                                pane.send()
                        }
                        Keys.onEnterPressed: event => Keys.returnPressed(event)

                        Label {
                            x: composer.leftPadding
                            y: composer.topPadding
                            visible: composer.text === "" && !composer.preeditText
                            text: backend.connectionState === "online"
                                  ? qsTr("Message %1").arg(backend.currentChatName)
                                  : qsTr("You're offline")
                            color: win.mutedColor
                            font: composer.font
                        }
                    }
                }
                AccentButton {
                    Layout.alignment: Qt.AlignBottom
                    text: qsTr("Send")
                    enabled: composer.text.trim() !== "" && backend.connectionState === "online"
                    onClicked: pane.send()
                }
            }
        }
    }

    // Dropping a file onto the conversation sends it.
    DropArea {
        anchors.fill: parent
        enabled: backend.currentChat !== ""
        onDropped: drop => {
            if (drop.hasUrls) {
                backend.sendFile(drop.urls[0])
                drop.accept()
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 8
            visible: parent.containsDrag
            radius: theme.radius
            color: Qt.alpha(theme.accent, 0.08)
            border.color: theme.accent
            border.width: 2
            Label {
                anchors.centerIn: parent
                text: qsTr("Drop to send to %1").arg(backend.currentChatName)
                color: win.textColor
                font.pixelSize: 16
            }
        }
    }
}
