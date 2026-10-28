import QtQml
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
    title: accounts.totalUnread > 0 ? "(%1) Omajim".arg(accounts.totalUnread) : "Omajim"

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

    // Closing the window does what the preferences say, as Gajim's
    // action_on_close: to the tray (connected in the background, like other
    // chat apps), minimized, or quit. Ctrl+Q always quits.
    onClosing: close => {
        if (backend.closeAction === "minimize") {
            close.accepted = false
            win.showMinimized()
        } else if (backend.closeAction === "tray" && tray.available && backend.signedIn) {
            close.accepted = false
            win.hide()
        } else {
            win.quit()
        }
    }

    // Qt.quit() asks the window to close first, and closing it only hides
    // it, which calls the quit off; exit() leaves without asking.
    function quit() {
        Qt.exit(0)
    }

    // A notification clicked, of whichever account (it comes up first).
    Connections {
        target: accounts
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
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: win.quit() }
    Shortcut { sequence: "Ctrl+Shift+K"; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: backend.showAllContacts = !backend.showAllContacts }
    Shortcut { sequences: ["Ctrl+K", "Ctrl+T"]; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: contactPane.focusSearch() }
    Shortcut { sequence: "Ctrl+B"; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: contactPane.setCollapsed(!contactPane.collapsed) }
    // Ctrl+1, Ctrl+2…: the accounts, in the order of their menu.
    Instantiator {
        model: Math.min(9, accounts.list.length)
        delegate: Shortcut {
            required property int index
            sequence: "Ctrl+" + (index + 1)
            context: Qt.ApplicationShortcut
            onActivated: accounts.selectIndex(index)
        }
    }
    Shortcut { sequence: "Ctrl+,"; context: Qt.ApplicationShortcut; onActivated: preferencesDialog.open() }
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
    Shortcut { sequence: "Ctrl+W"; context: Qt.ApplicationShortcut; enabled: backend.currentChat !== ""; onActivated: backend.closeConversation(backend.currentChat) }
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

        // The account on screen, in the middle like Hype's deck title; a
        // click lists them all, each connected on its own. Our status is at
        // the foot of the sidebar.
        ItemDelegate {
            id: accountButton
            visible: accounts.list.length > 0
            readonly property real room: Math.max(0, topBar.width - 2 * (Math.max(actions.width, logo.width) + win.inset + 20))
            anchors.centerIn: parent
            width: Math.min(implicitWidth, room)
            height: 36
            leftPadding: 12
            rightPadding: 8
            // The style's own vertical padding would push the row off center.
            topPadding: 0
            bottomPadding: 0
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTr("Accounts")
            // Below, its right edge with the button's.
            onClicked: accountMenu.toggle(accountButton, accountButton.width - accountMenu.implicitWidth,
                                          accountButton.height + 4)
            background: Rectangle {
                radius: theme.radius
                color: accountButton.hovered || accountMenu.visible ? win.ui.hover : "transparent"
            }
            contentItem: RowLayout {
                spacing: 6
                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    elide: Text.ElideRight
                    text: backend.jid !== "" ? backend.jid : qsTr("New account")
                    color: win.ui.foreground
                }
                // Messages waiting on the other accounts.
                Rectangle {
                    readonly property int others: accounts.totalUnread - backend.contacts.totalUnread
                    visible: others > 0
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Math.max(18, othersLabel.implicitWidth + 8)
                    implicitHeight: 18
                    radius: 9
                    color: win.ui.accent
                    Label {
                        id: othersLabel
                        anchors.centerIn: parent
                        text: parent.others
                        color: win.ui.accentText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
                AppIcon {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: 16
                    implicitHeight: 16
                    name: "chevron-down"
                    color: win.ui.muted
                }
            }

            // A popup, not a Menu: a Menu re-adds the lines of a changed
            // list at its end, below "Add another account".
            Popup {
                id: accountMenu
                property double closedAt: 0
                // As AppMenu's: the press that closed it doesn't reopen it.
                function toggle(button, x, y) {
                    if (visible)
                        close()
                    else if (Date.now() - closedAt > 300) {
                        accountMenu.x = x
                        accountMenu.y = y
                        open()
                    }
                }
                onClosed: closedAt = Date.now()
                padding: 6
                margins: 8
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
                background: Rectangle {
                    color: win.ui.panel
                    border.color: win.ui.border
                    radius: theme.radius
                }
                contentItem: ColumnLayout {
                spacing: 0
                Repeater {
                    model: accounts.list
                    // The address and its shortcut; a click shows the
                    // account. The rest is in Manage accounts.
                    ItemDelegate {
                        id: accountItem
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: 32
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 0
                        bottomPadding: 0
                        focusPolicy: Qt.NoFocus
                        implicitWidth: leftPadding + accountRow.implicitWidth + rightPadding
                        onClicked: {
                            accountMenu.close()
                            accounts.select(modelData.jid)
                        }
                        background: Rectangle {
                            radius: Math.min(4, theme.radius)
                            color: accountItem.modelData.current ? Qt.alpha(win.ui.accent, 0.15)
                                 : accountItem.hovered ? win.ui.hover : "transparent"
                        }
                        contentItem: RowLayout {
                            id: accountRow
                            spacing: 28
                            Label {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                text: accountItem.modelData.jid
                                color: accountItem.modelData.enabled ? win.ui.foreground : win.ui.muted
                                font.pixelSize: 13
                                font.weight: accountItem.modelData.current ? Font.DemiBold : Font.Normal
                            }
                            Label {
                                visible: accountItem.index < 9
                                Layout.alignment: Qt.AlignVCenter
                                text: "Ctrl+" + (accountItem.index + 1)
                                color: win.ui.muted
                                font.pixelSize: 12
                            }
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 5
                    Layout.bottomMargin: 5
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    implicitHeight: 1
                    color: win.ui.border
                }
                ItemDelegate {
                    id: manageItem
                    Layout.fillWidth: true
                    implicitHeight: 32
                    leftPadding: 12
                    topPadding: 0
                    bottomPadding: 0
                    focusPolicy: Qt.NoFocus
                    onClicked: {
                        accountMenu.close()
                        accountsDialog.open()
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: manageItem.hovered ? win.ui.hover : "transparent"
                    }
                    contentItem: Label {
                        text: qsTr("Manage accounts…")
                        color: win.ui.foreground
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                ItemDelegate {
                    id: addItem
                    Layout.fillWidth: true
                    implicitHeight: 32
                    leftPadding: 12
                    topPadding: 0
                    bottomPadding: 0
                    focusPolicy: Qt.NoFocus
                    enabled: !accounts.adding
                    onClicked: {
                        accountMenu.close()
                        accounts.addAccount()
                    }
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: addItem.hovered && addItem.enabled ? win.ui.hover : "transparent"
                    }
                    contentItem: Label {
                        text: qsTr("Add another account…")
                        color: win.ui.foreground
                        opacity: addItem.enabled ? 1 : 0.4
                        font.pixelSize: 13
                        verticalAlignment: Text.AlignVCenter
                    }
                }
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
                onClicked: moreMenu.toggle(moreButton, 0, moreButton.height + 4)

                AppMenu {
                    id: moreMenu
                    objectName: "moreMenu"
                    AppMenuItem { text: qsTr("Preferences…"); hint: "Ctrl+,"; onTriggered: preferencesDialog.open() }
                    AppMenuItem { text: qsTr("Keyboard shortcuts"); hint: "?"; onTriggered: shortcutsOverlay.open() }
                    AppMenuSeparator {}
                    AppMenuItem { text: qsTr("Quit"); hint: "Ctrl+Q"; destructive: true; onTriggered: win.quit() }
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
                    onGroupRequested: (jid, name, group) => {
                        groupDialog.jid = jid
                        groupDialog.name = name
                        groupField.text = group
                        groupDialog.open()
                    }
                    onAliasRequested: (jid, alias, rosterName) => {
                        aliasDialog.jid = jid
                        aliasDialog.rosterName = rosterName
                        aliasField.text = alias
                        aliasDialog.open()
                    }
                    Layout.preferredWidth: collapsed ? 68 : 300
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

        PreferencesDialog {
            id: preferencesDialog
        }

        AccountsDialog {
            id: accountsDialog
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

        AppDialog {
            id: groupDialog
            property string jid
            property string name
            title: qsTr("Move %1 to a group").arg(name)
            subtitle: qsTr("The group is kept on your server, so your other devices see it too.")
            acceptText: qsTr("Move")
            onOpened: {
                groupField.forceActiveFocus()
                groupField.selectAll()
            }
            onAccepted: backend.setContactGroup(jid, groupField.text)

            AppTextField {
                id: groupField
                Layout.fillWidth: true
                placeholderText: qsTr("Group name (empty for none)")
                onAccepted: groupDialog.accept()
            }
            // The groups there are, one click away.
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: groupDialog.visible ? backend.contacts.allGroups() : []
                    delegate: AppButton {
                        required property string modelData
                        implicitHeight: 30
                        flat: groupField.text !== modelData
                        text: modelData
                        onClicked: groupField.text = modelData
                    }
                }
            }
        }

        // A name for a contact on this computer only, like the Python
        // Omajim's local nickname; the roster keeps its own.
        AppDialog {
            id: aliasDialog
            property string jid
            property string rosterName
            title: qsTr("Nickname for %1").arg(rosterName)
            subtitle: qsTr("Only on this computer: the name on your server, %1, stays as it is.").arg(rosterName)
            acceptText: qsTr("Save")
            onOpened: {
                aliasField.forceActiveFocus()
                aliasField.selectAll()
            }
            onAccepted: backend.setAlias(jid, aliasField.text)

            AppTextField {
                id: aliasField
                Layout.fillWidth: true
                placeholderText: qsTr("Leave empty to use %1").arg(aliasDialog.rosterName)
                onAccepted: aliasDialog.accept()
            }
        }

        ShortcutsOverlay {
            id: shortcutsOverlay
            objectName: "shortcutsOverlay"
        }
    }
}
