import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The sidebar: a search field, requests and invitations waiting for an
// answer, then rooms and people.
Rectangle {
    id: pane

    readonly property var ui: theme.colors
    signal aliasRequested(string jid, string alias, string rosterName)
    signal groupRequested(string jid, string name, string group)
    // A search lists every contact and room; otherwise only the open
    // conversations show.
    readonly property bool searching: search.text.trim() !== ""
    color: ui.panel

    function sectionName(group) {
        return group === "#rooms" ? qsTr("Group chats") : group === "" ? qsTr("No group") : group
    }

    function focusSearch() {
        search.forceActiveFocus()
        search.selectAll()
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

        AppTextField {
            id: search
            objectName: "searchField"
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            implicitHeight: 36
            leftPadding: 34
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
                            font.bold: segment.active
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
                    AppMenuItem {
                        visible: !row.isRoom
                        height: visible ? implicitHeight : 0
                        text: qsTr("Move to group…")
                        onTriggered: pane.groupRequested(row.jid, row.name, row.group)
                    }
                    AppMenuItem {
                        visible: !pane.searching
                        height: visible ? implicitHeight : 0
                        text: qsTr("Close conversation")
                        hint: row.selected ? "Ctrl+W" : ""
                        onTriggered: backend.closeConversation(row.jid)
                    }
                    AppMenuItem {
                        text: row.isRoom ? qsTr("Leave %1").arg(row.name) : qsTr("Remove %1").arg(row.name)
                        destructive: true
                        onTriggered: row.isRoom ? backend.leaveRoom(row.jid) : backend.removeContact(row.jid)
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
                                font.weight: row.unread > 0 ? Font.Bold : Font.Normal
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
                            font.bold: true
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
    }
}
