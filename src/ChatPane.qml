import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The open conversation, on the theme's background like Hype's stage: who
// it's with, the messages, and the field to write in.
Rectangle {
    id: pane

    readonly property var ui: theme.colors
    color: ui.background

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
        spacing: 8
        AppIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            name: "message"
            color: pane.ui.border
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Pick a conversation")
            color: pane.ui.foreground
            font.pixelSize: 16
            font.bold: true
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Ctrl+K searches your contacts. ? lists every shortcut.")
            color: pane.ui.muted
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: backend.currentChat !== ""
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20
            Layout.rightMargin: 14
            spacing: 12

            ContactAvatar {
                implicitWidth: 32
                implicitHeight: 32
                jid: backend.currentChat
                name: backend.currentChatName
                source: backend.currentChatAvatar
                presence: backend.currentChatPresence
                isRoom: backend.currentChatIsRoom
                ringColor: pane.ui.background
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: backend.currentChatName
                    color: pane.ui.foreground
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                }
                // A room shows its subject, or whether we're in it; a person
                // their status, or that they're typing.
                Label {
                    Layout.fillWidth: true
                    text: backend.currentChatIsRoom
                          ? (backend.currentChatStatusText !== "" ? backend.currentChatStatusText
                             : backend.currentChatPresence === "online" ? qsTr("Group chat") : qsTr("Not in the room"))
                          : backend.currentChatTyping ? qsTr("typing…")
                          : backend.currentChatStatusText !== ""
                          ? "%1 — %2".arg(win.presenceLabel(backend.currentChatPresence)).arg(backend.currentChatStatusText)
                          : win.presenceLabel(backend.currentChatPresence)
                    color: backend.currentChatTyping && !backend.currentChatIsRoom ? pane.ui.accent : pane.ui.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            AppButton {
                id: peopleButton
                visible: backend.currentChatIsRoom && backend.currentRoomParticipants.length > 0
                flat: true
                implicitHeight: 32
                text: qsTr("%n people", "", backend.currentRoomParticipants.length)
                onClicked: participantsPopup.open()

                Popup {
                    id: participantsPopup
                    y: parent.height + 4
                    x: parent.width - width
                    width: 240
                    height: Math.min(360, participantList.contentHeight + 12)
                    padding: 6
                    background: Rectangle {
                        radius: theme.radius
                        color: pane.ui.panel
                        border.color: pane.ui.border
                    }
                    ListView {
                        id: participantList
                        anchors.fill: parent
                        clip: true
                        model: backend.currentRoomParticipants
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Label {
                            required property string modelData
                            width: ListView.view.width
                            height: 30
                            leftPadding: 10
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            color: pane.ui.foreground
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredHeight: 1
            color: pane.ui.border
        }

        ListView {
            id: messages
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            topMargin: 16
            bottomMargin: 16
            model: backend.messages
            boundsBehavior: Flickable.StopAtBounds
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
                color: pane.ui.muted
            }
        }

        // The message field, framed like Hype's editors: the panel color,
        // a border that takes the accent while you write.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 20
            implicitHeight: Math.min(180, composerRow.implicitHeight + 16)
            radius: theme.radius
            color: pane.ui.panel
            border.color: composer.activeFocus ? pane.ui.accent : pane.ui.border

            RowLayout {
                id: composerRow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                IconButton {
                    id: attachButton
                    Layout.alignment: Qt.AlignBottom
                    iconName: "paperclip"
                    description: qsTr("Send a file")
                    checked: sendMenu.visible
                    onClicked: sendMenu.visible ? sendMenu.close() : sendMenu.popup(attachButton, 0, -sendMenu.implicitHeight - 4)

                    AppMenu {
                        id: sendMenu
                        AppMenuItem {
                            text: qsTr("Through the server")
                            hint: "Ctrl+O"
                            enabled: backend.serverUploadAvailable
                            onTriggered: backend.chooseFileToSend("server")
                        }
                        // Straight to the person's device, as Pidgin takes
                        // files; a room has no device to send to.
                        AppMenuItem {
                            text: qsTr("Directly")
                            hint: "Ctrl+Shift+O"
                            enabled: !backend.currentChatIsRoom
                            onTriggered: backend.chooseFileToSend("direct")
                        }
                    }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 36
                    TextArea {
                        id: composer
                        wrapMode: TextEdit.Wrap
                        background: null
                        color: pane.ui.foreground
                        selectionColor: pane.ui.selection
                        selectedTextColor: pane.ui.selectionText
                        font.pixelSize: 14
                        verticalAlignment: TextEdit.AlignVCenter
                        onTextChanged: backend.setComposing(text.length > 0)
                        // Up in an empty field brings back our last line to correct.
                        Keys.onUpPressed: event => {
                            const last = text === "" ? backend.startEditingLast() : ""
                            if (last !== "") {
                                text = last
                                cursorPosition = length
                            } else {
                                event.accepted = false
                            }
                        }
                        Keys.onEscapePressed: event => {
                            if (backend.editing) {
                                backend.cancelEditing()
                                clear()
                            } else {
                                event.accepted = false
                            }
                        }
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
                            text: backend.editing ? qsTr("Correcting your last message (Esc cancels)")
                                  : backend.connectionState === "online" ? qsTr("Message %1").arg(backend.currentChatName)
                                  : qsTr("You're offline")
                            color: backend.editing ? pane.ui.accent : pane.ui.muted
                            font: composer.font
                        }
                    }
                }

                AppButton {
                    Layout.alignment: Qt.AlignBottom
                    primary: true
                    text: backend.editing ? qsTr("Correct") : qsTr("Send")
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
                backend.sendFile(drop.urls[0], backend.serverUploadAvailable || backend.currentChatIsRoom ? "server" : "direct")
                drop.accept()
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 12
            visible: parent.containsDrag
            radius: theme.radius
            color: Qt.alpha(pane.ui.accent, 0.08)
            border.color: pane.ui.accent
            border.width: 2
            Label {
                anchors.centerIn: parent
                text: qsTr("Drop to send to %1").arg(backend.currentChatName)
                color: pane.ui.foreground
                font.pixelSize: 16
            }
        }
    }
}
