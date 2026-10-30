import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The sidebar: a search field, requests and invitations waiting for an
// answer, then rooms and people. Collapsed, as in ZapFast, only the open
// conversations' pictures, and the button to bring it back.
Rectangle {
    id: pane

    readonly property var ui: theme.colors
    readonly property bool collapsed: backend.sidebarCollapsed
    // The Contacts tab, if it was on, comes back with the whole sidebar.
    property bool browsedBefore: false
    signal aliasRequested(string jid, string alias, string rosterName)
    // A search lists every contact and room; otherwise only the open
    // conversations show.
    readonly property bool searching: search.text.trim() !== ""
    color: ui.panel

    function sectionName(group) {
        return group === "#rooms" ? qsTr("Group chats") : group === "" ? qsTr("No group") : group
    }

    function editNickname() {
        nicknameField.text = backend.ownNickname
        nicknameDialog.open()
    }

    function focusSearch() {
        setCollapsed(false)
        search.forceActiveFocus()
        search.selectAll()
    }

    // Collapsed, it lists the conversations, whatever was searched or
    // browsed.
    function setCollapsed(collapsed) {
        if (collapsed === pane.collapsed)
            return
        if (collapsed) {
            search.text = ""
            browsedBefore = backend.showAllContacts
            backend.showAllContacts = false
        }
        backend.sidebarCollapsed = collapsed
        if (!collapsed && browsedBefore)
            backend.showAllContacts = true
    }

    // Our own picture at the foot of the sidebar, as in ZapFast; a click
    // sets our status, or changes our picture or nickname. Expanded, our name and status beside it,
    // like Discord's user panel; the full address in the tooltip.
    component OwnAvatar: ItemDelegate {
        id: own
        property bool compact: false

        Layout.fillWidth: true
        implicitHeight: 52
        leftPadding: compact ? 0 : 20
        rightPadding: compact ? 0 : 18
        // The style's own vertical padding would push the row off center.
        topPadding: 0
        bottomPadding: 0
        focusPolicy: Qt.NoFocus
        Accessible.name: qsTr("Your avatar")
        // Above it; expanded, its right edge under the arrow's.
        onClicked: avatarMenu.toggle(own, own.compact ? 0 : own.width - avatarMenu.implicitWidth - 8,
                                     -avatarMenu.implicitHeight - 4)

        readonly property bool lit: own.hovered || avatarMenu.visible
        // Inset and rounded, like the line above it, not edge to edge.
        background: Rectangle {
            anchors.fill: parent
            anchors.leftMargin: own.compact ? 10 : 8
            anchors.rightMargin: own.compact ? 10 : 8
            radius: Math.min(6, theme.radius)
            color: own.lit ? pane.ui.hover : "transparent"
        }
        contentItem: RowLayout {
            spacing: 12
            ContactAvatar {
                Layout.alignment: own.compact ? Qt.AlignCenter : Qt.AlignVCenter
                Layout.fillWidth: own.compact
                implicitWidth: 34
                implicitHeight: 34
                Layout.maximumWidth: 34
                jid: backend.jid
                name: backend.jid
                source: backend.ownAvatar
                presence: win.ownPresence
                ringColor: own.lit ? pane.ui.hover : pane.ui.panel
            }
            ColumnLayout {
                visible: !own.compact
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: backend.ownNickname !== "" ? backend.ownNickname : backend.jid.split("@")[0]
                    color: pane.ui.foreground
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: win.presenceLabel(win.ownPresence)
                    color: pane.ui.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            // It opens a menu above it.
            AppIcon {
                visible: !own.compact
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 16
                implicitHeight: 16
                name: "chevron-up"
                color: own.lit ? pane.ui.foreground : pane.ui.muted
            }
        }
        // Collapsed, beside the column like the conversations' names.
        ToolTip {
            visible: own.hovered && !avatarMenu.visible
            delay: 600
            text: backend.jid
            x: own.compact ? own.width + 4 : (own.width - width) / 2
            y: own.compact ? (own.height - height) / 2 : -height - 4
        }

        AppMenu {
            id: avatarMenu
            hasChecks: true
            Repeater {
                model: ["online", "chat", "away", "xa", "dnd", "offline"]
                delegate: AppMenuItem {
                    required property string modelData
                    text: win.presenceLabel(modelData)
                    presence: modelData
                    checkable: true
                    checked: backend.status === modelData
                    onTriggered: accounts.setStatus(modelData)
                }
            }
            AppMenuSeparator {}
            AppMenuItem {
                text: qsTr("Change nickname…")
                enabled: backend.connectionState === "online"
                onTriggered: pane.editNickname()
            }
            AppMenuSeparator {}
            AppMenuItem {
                text: qsTr("Change avatar…")
                enabled: backend.connectionState === "online"
                onTriggered: backend.chooseAvatar()
            }
            AppMenuItem {
                text: qsTr("Remove avatar")
                enabled: backend.connectionState === "online" && backend.ownAvatar !== ""
                destructive: true
                onTriggered: backend.removeAvatar()
            }
        }
    }

    // A pending question (a contact request or a room invitation), as a
    // card with its two answers.
    component RequestCard: Rectangle {
        id: card
        property string message
        property string detail
        property string acceptText
        signal accept()
        signal decline()

        Layout.fillWidth: true
        Layout.leftMargin: 12
        Layout.rightMargin: 12
        implicitHeight: cardColumn.implicitHeight + 24
        radius: theme.radius
        color: pane.ui.background
        border.color: pane.ui.border

        ColumnLayout {
            id: cardColumn
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: card.message
                color: pane.ui.foreground
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: card.detail
                color: pane.ui.muted
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.topMargin: 4
                spacing: 8
                Item { Layout.fillWidth: true }
                AppButton {
                    flat: true
                    implicitHeight: 32
                    text: qsTr("Decline")
                    onClicked: card.decline()
                }
                AppButton {
                    primary: true
                    implicitHeight: 32
                    text: card.acceptText
                    onClicked: card.accept()
                }
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        spacing: 10
        visible: !pane.collapsed

        AppTextField {
            id: search
            objectName: "searchField"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            implicitHeight: 36
            leftPadding: 34
            rightPadding: text !== "" ? 34 : 12
            placeholderText: qsTr("Search contacts")
            onTextChanged: backend.contacts.filter = text
            Keys.onDownPressed: {
                list.forceActiveFocus()
                list.currentIndex = 0
            }
            Keys.onEscapePressed: text = ""
            // Enter opens the first match, or a conversation with a full
            // address nobody has yet, like Gajim's Start Chat.
            onAccepted: {
                if (backend.contacts.count > 0)
                    backend.openChat(backend.contacts.jidAt(0))
                else if (/^[^@\s]+@[^@\s]+$/.test(text.trim()))
                    backend.openChat(text.trim())
                else
                    return
                text = ""
            }

            AppIcon {
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                name: "search"
                color: pane.ui.muted
            }
            // The search cleared at a click, as in ZapFast.
            IconButton {
                visible: search.text !== ""
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 28
                implicitHeight: 28
                focusPolicy: Qt.NoFocus
                iconName: "close"
                description: qsTr("Clear the search (Esc)")
                onClicked: {
                    search.text = ""
                    search.forceActiveFocus()
                }
            }
        }

        // Conversations | Contacts: what the list shows while nothing is
        // searched. Arrows and Alt+Up/Down move within it.
        Rectangle {
            id: modeSwitch
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            implicitHeight: 32
            radius: Math.min(4, theme.radius)
            color: pane.ui.background
            border.color: pane.ui.border

            Row {
                anchors.fill: parent
                anchors.margins: 3
                Repeater {
                    model: [{label: qsTr("Conversations"), all: false}, {label: qsTr("Contacts"), all: true}]
                    delegate: Rectangle {
                        id: segment
                        required property var modelData
                        readonly property bool active: backend.showAllContacts === modelData.all
                        width: (modeSwitch.width - 6) / 2
                        height: parent.height
                        radius: Math.min(3, theme.radius)
                        color: active ? pane.ui.selection : segmentHover.hovered ? pane.ui.hover : "transparent"
                        Label {
                            anchors.centerIn: parent
                            text: segment.modelData.label
                            font.pixelSize: 13
                            font.weight: segment.active ? Font.DemiBold : Font.Normal
                            color: segment.active ? pane.ui.selectionText : pane.ui.muted
                        }
                        HoverHandler { id: segmentHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: backend.showAllContacts = segment.modelData.all }
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: segment.modelData.label
                        Accessible.checked: segment.active
                    }
                }
            }
            ToolTip.visible: switchHover.hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Ctrl+Shift+K switches")
            HoverHandler { id: switchHover }
        }

        Repeater {
            model: backend.subscriptionRequests
            delegate: RequestCard {
                required property string modelData
                message: qsTr("%1 wants to add you as a contact.").arg(modelData)
                acceptText: qsTr("Accept")
                onAccept: backend.acceptRequest(modelData)
                onDecline: backend.refuseRequest(modelData)
            }
        }

        Repeater {
            model: backend.invitations
            delegate: RequestCard {
                required property var modelData
                required property int index
                message: qsTr("%1 invites you to %2").arg(modelData.from).arg(modelData.room)
                detail: modelData.reason
                acceptText: qsTr("Join")
                onAccept: backend.acceptInvitation(index)
                onDecline: backend.declineInvitation(index)
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.contacts
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Keys.onReturnPressed: backend.openChat(backend.contacts.jidAt(currentIndex))

            section.property: backend.contacts.browsing && !pane.searching ? "group" : ""
            section.criteria: ViewSection.FullString
            section.delegate: Label {
                required property string section
                width: ListView.view.width
                leftPadding: 20
                topPadding: 14
                bottomPadding: 6
                text: pane.sectionName(section).toUpperCase()
                font.pixelSize: 11
                font.letterSpacing: 1
                color: pane.ui.accent
            }

            delegate: ItemDelegate {
                id: row
                required property int index
                required property string jid
                required property string name
                required property string presence
                required property string statusText
                required property int unread
                required property string avatar
                required property bool isRoom
                required property var lastActivity
                required property string lastMessage
                required property bool lastOutgoing
                required property string alias
                required property string rosterName
                required property string group

                readonly property bool selected: jid === backend.currentChat
                readonly property bool focusedRow: list.activeFocus && ListView.isCurrentItem
                readonly property color ink: selected ? pane.ui.selectionText : pane.ui.foreground

                width: ListView.view.width
                height: 56
                // Taking focus would end browsing before the click opens the chat.
                focusPolicy: Qt.NoFocus
                leftPadding: 20
                rightPadding: 16
                onClicked: {
                    backend.openChat(jid)
                    search.text = ""
                }

                background: Rectangle {
                    color: row.selected ? pane.ui.selection
                         : row.hovered || row.focusedRow ? pane.ui.hover : "transparent"
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: contactMenu.popup()
                }
                AppMenu {
                    id: contactMenu
                    AppMenuItem {
                        text: qsTr("Set a nickname…")
                        onTriggered: pane.aliasRequested(row.jid, row.alias, row.rosterName)
                    }
                    // No moving people between groups: here they're the
                    // company's departments, set on the server.
                    AppMenuItem {
                        visible: !pane.searching
                        height: visible ? implicitHeight : 0
                        text: qsTr("Close conversation")
                        hint: row.selected ? "Ctrl+W" : ""
                        onTriggered: backend.closeConversation(row.jid)
                    }
                    // Contacts aren't removed from here: in a company the
                    // list is everyone's, not one person's to trim.
                    AppMenuItem {
                        visible: row.isRoom
                        height: visible ? implicitHeight : 0
                        text: qsTr("Leave %1").arg(row.name)
                        destructive: true
                        onTriggered: backend.leaveRoom(row.jid)
                    }
                }

                contentItem: RowLayout {
                    spacing: 12
                    ContactAvatar {
                        jid: row.jid
                        name: row.name
                        source: row.avatar
                        presence: row.presence
                        isRoom: row.isRoom
                        // The ring takes the row's color, so the dot stands apart.
                        ringColor: row.background.color.a > 0 ? row.background.color : pane.ui.panel
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Label {
                                Layout.fillWidth: true
                                text: row.name
                                color: row.ink
                                elide: Text.ElideRight
                                font.weight: row.unread > 0 ? Font.DemiBold : Font.Normal
                            }
                            // When the newest line was written: the time
                            // today, the date before.
                            Label {
                                visible: !pane.searching && text !== ""
                                text: {
                                    translator.language
                                    const stamp = row.lastActivity
                                    if (!stamp || isNaN(stamp.getTime()) || row.lastMessage === "")
                                        return ""
                                    return new Date().toDateString() === stamp.toDateString()
                                           ? stamp.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                                           : stamp.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
                                }
                                color: row.selected ? row.ink : pane.ui.muted
                                opacity: row.selected ? 0.75 : 1
                                font.pixelSize: 11
                            }
                        }
                        // The newest line in the list of conversations; the
                        // status or address while searching.
                        Label {
                            Layout.fillWidth: true
                            text: !pane.searching && row.lastMessage !== ""
                                  ? (row.lastOutgoing ? qsTr("You: %1").arg(row.lastMessage) : row.lastMessage)
                                  : [pane.searching && row.group !== "" && row.group !== "#rooms" ? row.group : "",
                                     row.statusText !== "" ? row.statusText : row.jid].filter(part => part !== "").join(" · ")
                            color: row.selected ? row.ink : pane.ui.muted
                            opacity: row.selected ? 0.75 : 1
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        visible: row.unread > 0
                        implicitWidth: Math.max(20, unreadLabel.implicitWidth + 10)
                        implicitHeight: 20
                        radius: 10
                        color: pane.ui.accent
                        Label {
                            id: unreadLabel
                            anchors.centerIn: parent
                            text: translator.language, row.unread.toLocaleString(Qt.locale(), 'f', 0)
                            color: pane.ui.accentText
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 40
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: pane.ui.muted
                text: /^[^@\s]+@[^@\s]+$/.test(search.text.trim()) ? qsTr("Enter starts a conversation with %1.").arg(search.text.trim())
                    : search.text !== "" ? qsTr("No contact matches “%1”.").arg(search.text)
                    : backend.showAllContacts ? qsTr("No contacts yet. Ctrl+N adds one.")
                    : qsTr("No open conversations. Search for a contact to start one (Ctrl+K).")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.preferredHeight: 1
            color: pane.ui.border
        }
        // As much room above as below (the column's spacing is 10).
        OwnAvatar {
            Layout.topMargin: -2
            Layout.bottomMargin: 8
        }
    }

    // Collapsed: the open conversations as pictures, the one shown marked
    // at the edge, as in ZapFast.
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        spacing: 8
        visible: pane.collapsed

        IconButton {
            Layout.alignment: Qt.AlignHCenter
            iconName: "sidebar"
            description: qsTr("Show the sidebar (Ctrl+B)")
            onClicked: pane.setCollapsed(false)
        }
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 1
            color: pane.ui.border
        }

        ListView {
            id: rail
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: pane.collapsed ? backend.contacts : null
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: railItem
                required property string jid
                required property string name
                required property string presence
                required property int unread
                required property string avatar
                required property bool isRoom
                readonly property bool selected: jid === backend.currentChat

                width: ListView.view.width
                height: 48

                Rectangle {
                    visible: railItem.selected
                    width: 3
                    height: 26
                    radius: 1.5
                    anchors.verticalCenter: parent.verticalCenter
                    color: pane.ui.accent
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 46
                    height: 46
                    radius: Math.min(8, theme.radius)
                    color: railItem.selected ? pane.ui.selection : railHover.hovered ? pane.ui.hover : "transparent"
                }
                ContactAvatar {
                    anchors.centerIn: parent
                    implicitWidth: 38
                    implicitHeight: 38
                    jid: railItem.jid
                    name: railItem.name
                    source: railItem.avatar
                    presence: railItem.presence
                    isRoom: railItem.isRoom
                    ringColor: railItem.selected ? pane.ui.selection : railHover.hovered ? pane.ui.hover : pane.ui.panel
                }
                // New lines waiting, over the picture's corner.
                Rectangle {
                    visible: railItem.unread > 0
                    x: parent.width / 2 + 8
                    y: 2
                    implicitWidth: Math.max(18, railUnread.implicitWidth + 8)
                    implicitHeight: 18
                    radius: 9
                    color: pane.ui.accent
                    border.color: pane.ui.panel
                    Label {
                        id: railUnread
                        anchors.centerIn: parent
                        text: translator.language, railItem.unread.toLocaleString(Qt.locale(), 'f', 0)
                        color: pane.ui.accentText
                        font.pixelSize: 10
                        font.weight: Font.DemiBold
                    }
                }

                HoverHandler { id: railHover; cursorShape: Qt.PointingHandCursor }
                // The name beside the column, not over the next picture.
                ToolTip {
                    visible: railHover.hovered
                    delay: 300
                    text: railItem.name
                    x: railItem.width + 4
                    y: (railItem.height - height) / 2
                }
                TapHandler { onTapped: backend.openChat(railItem.jid) }
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 32
            Layout.preferredHeight: 1
            color: pane.ui.border
        }
        // As much room above as below (the column's spacing is 8).
        OwnAvatar {
            compact: true
            Layout.bottomMargin: 8
        }
    }

    AppDialog {
        id: nicknameDialog
        title: qsTr("Your nickname")
        subtitle: qsTr("How contacts see you when they haven't named you themselves.")
        acceptText: qsTr("Save")
        onAboutToShow: nicknameField.forceActiveFocus()
        onAccepted: backend.setNickname(nicknameField.text)

        AppTextField {
            id: nicknameField
            Layout.fillWidth: true
            placeholderText: backend.jid.split("@")[0]
            onAccepted: nicknameDialog.accept()
        }
    }
}
