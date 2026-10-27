import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: pane
    color: win.panelColor
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    signal addContactRequested()

    function focusSearch() {
        search.forceActiveFocus()
        search.selectAll()
    }

    readonly property string ownPresence: backend.connectionState === "online" ? backend.status
                                         : backend.connectionState === "connecting" ? "connecting" : "offline"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            Layout.bottomMargin: 4
            spacing: 10

            PresenceDot { presence: pane.ownPresence }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    Layout.fillWidth: true
                    text: backend.jid
                    color: win.textColor
                    elide: Text.ElideRight
                    font.weight: Font.Medium
                }
                Label {
                    Layout.fillWidth: true
                    text: win.presenceLabel(pane.ownPresence)
                    color: win.mutedColor
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            ToolButton {
                Material.roundedScale: theme.radius
                text: "⋮"
                font.pixelSize: 18
                onClicked: accountMenu.popup()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Status and account")

                Menu {
                    id: accountMenu
                    Material.roundedScale: theme.radius
                    Repeater {
                        model: ["online", "chat", "away", "xa", "dnd", "offline"]
                        MenuItem {
                            required property string modelData
                            text: win.presenceLabel(modelData)
                            checkable: true
                            checked: backend.status === modelData
                            onTriggered: backend.setStatus(modelData)
                        }
                    }
                    MenuSeparator {}
                    MenuItem {
                        text: qsTr("Add contact…")
                        onTriggered: pane.addContactRequested()
                    }
                    MenuItem {
                        text: qsTr("Sign out")
                        onTriggered: backend.signOut()
                    }
                }
            }
        }

        Repeater {
            model: backend.subscriptionRequests
            delegate: Rectangle {
                required property string modelData
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.topMargin: 6
                implicitHeight: requestColumn.implicitHeight + 16
                radius: theme.radius
                color: win.surfaceColor

                ColumnLayout {
                    id: requestColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("%1 wants to add you as a contact.").arg(modelData)
                        color: win.textColor
                        wrapMode: Text.WrapAnywhere
                    }
                    RowLayout {
                        Item { Layout.fillWidth: true }
                        Button {
                            Material.roundedScale: theme.radius
                            text: qsTr("Refuse")
                            flat: true
                            onClicked: backend.refuseRequest(modelData)
                        }
                        AccentButton {
                            text: qsTr("Accept")
                            onClicked: backend.acceptRequest(modelData)
                        }
                    }
                }
            }
        }

        TextField {
            Material.roundedScale: Math.min(4, theme.radius)
            id: search
            Layout.fillWidth: true
            Layout.margins: 8
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
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.contacts
            keyNavigationEnabled: true
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

                width: ListView.view.width
                height: 54
                highlighted: jid === backend.currentChat || (list.activeFocus && ListView.isCurrentItem)
                onClicked: backend.openChat(jid)

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: contactMenu.popup()
                }
                Menu {
                    id: contactMenu
                    Material.roundedScale: theme.radius
                    MenuItem {
                        text: qsTr("Remove %1").arg(row.name)
                        onTriggered: backend.removeContact(row.jid)
                    }
                }

                contentItem: RowLayout {
                    spacing: 10
                    PresenceDot { presence: row.presence }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: row.name
                            color: win.textColor
                            elide: Text.ElideRight
                            font.weight: row.unread > 0 ? Font.DemiBold : Font.Normal
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.statusText !== "" ? row.statusText : row.jid
                            color: win.mutedColor
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                    Rectangle {
                        visible: row.unread > 0
                        implicitWidth: Math.max(20, unreadLabel.implicitWidth + 10)
                        implicitHeight: 20
                        radius: 10
                        color: theme.accent
                        Label {
                            id: unreadLabel
                            anchors.centerIn: parent
                            text: translator.language, row.unread.toLocaleString(Qt.locale(), 'f', 0)
                            color: theme.accentForeground
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                visible: list.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: win.mutedColor
                text: search.text !== "" ? qsTr("No contact matches “%1”.").arg(search.text)
                    : backend.connectionState === "online" ? qsTr("No contacts yet. Ctrl+N adds one.")
                    : qsTr("Contacts show up once you're connected.")
            }
        }
    }
}
