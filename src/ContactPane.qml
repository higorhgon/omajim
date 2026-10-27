import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The sidebar: a search field, requests and invitations waiting for an
// answer, then rooms and people.
Rectangle {
    id: pane

    readonly property var ui: theme.colors
    color: ui.panel

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
            onAccepted: {
                if (backend.contacts.count > 0) {
                    backend.openChat(backend.contacts.jidAt(0))
                    text = ""
                }
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

                readonly property bool selected: jid === backend.currentChat
                readonly property bool focusedRow: list.activeFocus && ListView.isCurrentItem
                readonly property color ink: selected ? pane.ui.selectionText : pane.ui.foreground

                width: ListView.view.width
                height: 56
                leftPadding: 20
                rightPadding: 16
                onClicked: backend.openChat(jid)

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
                        Label {
                            Layout.fillWidth: true
                            text: row.name
                            color: row.ink
                            elide: Text.ElideRight
                            font.weight: row.unread > 0 ? Font.DemiBold : Font.Normal
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.statusText !== "" ? row.statusText : row.jid
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
                text: search.text !== "" ? qsTr("No contact matches “%1”.").arg(search.text)
                    : backend.connectionState === "online" ? qsTr("No contacts yet. Ctrl+N adds one.")
                    : qsTr("Contacts show up once you're connected.")
            }
        }
    }
}
