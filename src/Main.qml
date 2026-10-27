import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1100
    height: 720
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: backend.contacts.totalUnread > 0 ? "(%1) Omajim".arg(backend.contacts.totalUnread) : "Omajim"

    // The whole window takes the Omarchy theme's palette, as Hype does.
    readonly property var ui: theme.colors
    readonly property int inset: 20
    color: ui.panel
    palette.window: ui.panel
    palette.base: ui.background
    palette.text: ui.foreground
    palette.windowText: ui.foreground
    palette.placeholderText: ui.muted
    palette.button: ui.button
    palette.buttonText: ui.foreground
    palette.highlight: ui.selection
    palette.highlightedText: ui.selectionText
    palette.mid: ui.hover
    palette.light: ui.border
    palette.dark: ui.button
    palette.toolTipBase: ui.panel
    palette.toolTipText: ui.foreground

    // Typing goes to a text field, so single-key shortcuts stand down.
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.cursorPosition !== undefined
    readonly property string ownPresence: backend.connectionState === "online" ? backend.status
                                         : backend.connectionState === "connecting" ? "connecting" : "offline"

    function presenceLabel(presence) {
        switch (presence) {
        case "online": return qsTr("Available")
        case "chat": return qsTr("Free to chat")
        case "away": return qsTr("Away")
        case "xa": return qsTr("Not available")
        case "dnd": return qsTr("Busy")
        case "connecting": return qsTr("Connecting…")
        }
        return qsTr("Offline")
    }

    function moveChat(step) {
        const contacts = backend.contacts
        if (contacts.count === 0)
            return
        const row = contacts.rowOf(backend.currentChat)
        const next = row < 0 ? (step > 0 ? 0 : contacts.count - 1)
                             : Math.max(0, Math.min(contacts.count - 1, row + step))
        backend.openChat(contacts.jidAt(next))
    }

    onActiveChanged: backend.windowActive = active

    // With the tray item showing, closing the window keeps Omajim connected
    // in the background, like other chat apps; Ctrl+Q quits for real.
    onClosing: close => {
        if (tray.available && backend.signedIn) {
            close.accepted = false
            win.hide()
        } else {
            Qt.quit()
        }
    }

    Connections {
        target: backend
        function onRaiseRequested() {
            win.show()
            win.raise()
            win.requestActivate()
        }
    }

    // ? opens the shortcuts like in Omarchy's other apps, except while
    // typing, where it's a character; F1 and Ctrl+/ work everywhere.
    Shortcut { sequences: ["?", "Shift+?"]; enabled: !win.typing; onActivated: shortcutsOverlay.open() }
    Shortcut { sequences: ["F1", "Ctrl+/"]; context: Qt.ApplicationShortcut; onActivated: shortcutsOverlay.visible ? shortcutsOverlay.close() : shortcutsOverlay.open() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+K"; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: contactPane.focusSearch() }
    Shortcut { sequence: "Ctrl+N"; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: addContactDialog.open() }
    Shortcut { sequence: "Ctrl+J"; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: joinRoomDialog.open() }
    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        enabled: backend.signedIn && backend.currentChat !== ""
        onActivated: backend.chooseFileToSend("server")
    }
    Shortcut {
        sequence: "Ctrl+Shift+O"
        context: Qt.ApplicationShortcut
        enabled: backend.signedIn && backend.currentChat !== "" && !backend.currentChatIsRoom
        onActivated: backend.chooseFileToSend("direct")
    }
    Shortcut { sequence: "Ctrl+W"; context: Qt.ApplicationShortcut; enabled: backend.currentChat !== ""; onActivated: backend.closeChat() }
    Shortcut { sequences: ["Alt+Down", "Ctrl+Tab"]; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: win.moveChat(1) }
    Shortcut { sequences: ["Alt+Up", "Ctrl+Shift+Tab"]; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: win.moveChat(-1) }

    header: ToolBar {
        id: topBar
        height: 60
        background: Rectangle {
            color: win.ui.panel
            Rectangle {
                x: win.inset
                width: parent.width - 2 * win.inset
                height: 1
                anchors.bottom: parent.bottom
                color: win.ui.border
            }
        }

        Label {
            id: logo
            anchors.left: parent.left
            anchors.leftMargin: win.inset
            anchors.verticalCenter: parent.verticalCenter
            text: "Omajim"
            font.pixelSize: 22
            font.bold: true
            color: logoHover.hovered ? win.ui.accentHover : win.ui.accent
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Keyboard shortcuts")
            ToolTip.visible: logoHover.hovered
            ToolTip.delay: 400
            ToolTip.text: qsTr("Keyboard shortcuts (?)")
            HoverHandler { id: logoHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: shortcutsOverlay.open() }
        }

        // Who you are and how you're connected, in the middle like Hype's
        // deck title and status.
        Column {
            id: summary
            visible: backend.signedIn
            readonly property real room: Math.max(0, topBar.width - 2 * (Math.max(actions.width, logo.width) + win.inset + 20))
            anchors.centerIn: parent
            spacing: 2
            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, summary.room)
                elide: Text.ElideRight
                text: backend.jid
                color: win.ui.foreground
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6
                PresenceDot {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    presence: win.ownPresence
                }
                Label {
                    text: win.presenceLabel(win.ownPresence)
                    font.pixelSize: 12
                    color: win.ui.muted
                }
            }
        }

        RowLayout {
            id: actions
            visible: backend.signedIn
            anchors.right: parent.right
            anchors.rightMargin: win.inset
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6

            StatusPicker {}
            IconButton {
                iconName: "user-add"
                description: qsTr("Add a contact (Ctrl+N)")
                onClicked: addContactDialog.open()
            }
            IconButton {
                iconName: "users"
                description: qsTr("Join a group chat (Ctrl+J)")
                onClicked: joinRoomDialog.open()
            }
            IconButton {
                id: moreButton
                iconName: "more"
                description: qsTr("More")
                checked: moreMenu.visible
                onClicked: moreMenu.visible ? moreMenu.close() : moreMenu.popup(moreButton, 0, moreButton.height + 4)

                AppMenu {
                    id: moreMenu
                    objectName: "moreMenu"
                    AppMenuItem { text: qsTr("Keyboard shortcuts"); hint: "?"; onTriggered: shortcutsOverlay.open() }
                    AppMenuSeparator {}
                    AppMenuItem { text: qsTr("Sign out"); onTriggered: backend.signOut() }
                    AppMenuItem { text: qsTr("Quit"); hint: "Ctrl+Q"; destructive: true; onTriggered: Qt.quit() }
                }
            }
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // What went wrong, in the window: a line in the theme's error
            // color, with the action that fixes it when there is one.
            Rectangle {
                Layout.fillWidth: true
                visible: backend.errorText !== ""
                implicitHeight: bannerRow.implicitHeight + 20
                color: Qt.tint(win.ui.panel, Qt.alpha(win.ui.error, 0.14))

                Rectangle {
                    width: 3
                    height: parent.height
                    color: win.ui.error
                }

                RowLayout {
                    id: bannerRow
                    anchors.fill: parent
                    anchors.leftMargin: win.inset
                    anchors.rightMargin: win.inset - 8
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: backend.errorText
                            color: win.ui.foreground
                            wrapMode: Text.Wrap
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: backend.certificateFingerprint !== ""
                            text: qsTr("SHA-256 fingerprint: %1").arg(backend.certificateFingerprint)
                            color: win.ui.muted
                            font.family: "monospace"
                            font.pixelSize: 11
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    AppButton {
                        visible: backend.certificateFingerprint !== ""
                        text: qsTr("Stay offline")
                        onClicked: backend.rejectCertificate()
                    }
                    AppButton {
                        visible: backend.certificateFingerprint !== ""
                        primary: true
                        text: qsTr("Trust this certificate")
                        onClicked: backend.trustCertificate()
                    }
                    IconButton {
                        visible: backend.certificateFingerprint === ""
                        iconName: "close"
                        description: qsTr("Dismiss")
                        onClicked: backend.dismissError()
                    }
                }
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignCenter
                Layout.fillHeight: true
                visible: backend.starting
                running: visible
            }

            SignInView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !backend.starting && !backend.signedIn
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: backend.signedIn
                spacing: 0

                ContactPane {
                    id: contactPane
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                }
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: win.ui.border
                }
                ChatPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }

        AppDialog {
            id: addContactDialog
            objectName: "addContactDialog"
            title: qsTr("Add contact")
            subtitle: qsTr("They'll be asked to share their status with you.")
            acceptText: qsTr("Add")
            acceptEnabled: newJid.text.indexOf("@") > 0
            onOpened: {
                newJid.text = ""
                newName.text = ""
                newJid.forceActiveFocus()
            }
            onAccepted: backend.addContact(newJid.text, newName.text)

            AppTextField {
                id: newJid
                Layout.fillWidth: true
                placeholderText: qsTr("Address (name@server)")
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: addContactDialog.accept()
            }
            AppTextField {
                id: newName
                Layout.fillWidth: true
                placeholderText: qsTr("Name (optional)")
                onAccepted: addContactDialog.accept()
            }
        }

        AppDialog {
            id: joinRoomDialog
            title: qsTr("Join a group chat")
            subtitle: qsTr("The room is saved on your server, so it comes back on your other devices and next time.")
            acceptText: qsTr("Join")
            acceptEnabled: roomJid.text.indexOf("@") > 0
            onOpened: {
                roomJid.text = ""
                roomNick.text = ""
                roomPassword.text = ""
                roomJid.forceActiveFocus()
            }
            onAccepted: backend.joinRoom(roomJid.text, roomNick.text, roomPassword.text)

            AppTextField {
                id: roomJid
                Layout.fillWidth: true
                placeholderText: qsTr("Room address (room@server)")
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: joinRoomDialog.accept()
            }
            AppTextField {
                id: roomNick
                Layout.fillWidth: true
                placeholderText: qsTr("Nickname (optional)")
                onAccepted: joinRoomDialog.accept()
            }
            AppTextField {
                id: roomPassword
                Layout.fillWidth: true
                placeholderText: qsTr("Room password (optional)")
                echoMode: TextInput.Password
                onAccepted: joinRoomDialog.accept()
            }
        }

        ShortcutsOverlay {
            id: shortcutsOverlay
            objectName: "shortcutsOverlay"
        }
    }
}
