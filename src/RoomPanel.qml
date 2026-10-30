import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The open room's people, under its header: what each one is there, and
// what we may do about them (expel, ban, moderator, member), as Gajim's
// occupant menu; and the room itself: invite, its settings, close it.
Popup {
    id: panel

    readonly property var ui: theme.colors
    property var people: []
    property var powers: ({})
    readonly property bool moderates: !!powers.setRoles || !!powers.setAffiliations

    function refresh() {
        people = backend.roomParticipants()
        powers = backend.roomPowers()
    }

    function standing(person) {
        switch (person.affiliation) {
        case "owner": return qsTr("owner")
        case "admin": return qsTr("admin")
        }
        if (person.role === "moderator")
            return qsTr("moderator")
        if (person.affiliation === "member")
            return qsTr("member")
        return person.role === "visitor" ? qsTr("visitor") : ""
    }

    width: 320
    height: Math.min(460, content.implicitHeight + topPadding + bottomPadding)
    padding: 6
    onAboutToShow: refresh()
    background: Rectangle {
        radius: theme.radius
        color: panel.ui.panel
        border.color: panel.ui.border
    }

    Connections {
        target: backend
        function onParticipantsChanged() { if (panel.visible) panel.refresh() }
        function onRoomSettingsRead(settings) {
            if (settings.room === backend.currentChat)
                roomSettingsDialog.openToChange(settings, backend.roomPowers())
        }
    }

    contentItem: ColumnLayout {
        id: content
        spacing: 4

        // The room: invite, settings, close.
        RowLayout {
            Layout.fillWidth: true
            spacing: 2
            AppButton {
                flat: true
                implicitHeight: 32
                text: qsTr("Invite…")
                onClicked: {
                    panel.close()
                    inviteDialog.open()
                }
            }
            AppButton {
                flat: true
                implicitHeight: 32
                visible: !!panel.powers.configure || !!panel.powers.changeSubject
                text: qsTr("Settings…")
                onClicked: {
                    panel.close()
                    backend.requestRoomSettings()
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                flat: true
                destructive: true
                implicitHeight: 32
                visible: !!panel.powers.configure
                text: qsTr("Close…")
                onClicked: {
                    panel.close()
                    closeDialog.open()
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: panel.ui.border
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(380, contentHeight)
            clip: true
            model: panel.people
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: ItemDelegate {
                id: row
                required property var modelData
                width: ListView.view.width
                implicitHeight: 34
                leftPadding: 10
                rightPadding: 4
                topPadding: 0
                bottomPadding: 0
                focusPolicy: Qt.NoFocus
                background: Rectangle {
                    radius: Math.min(4, theme.radius)
                    color: row.hovered ? panel.ui.hover : "transparent"
                }
                contentItem: RowLayout {
                    spacing: 8
                    Label {
                        Layout.fillWidth: true
                        text: row.modelData.self ? qsTr("%1 (you)").arg(row.modelData.nickname) : row.modelData.nickname
                        color: panel.ui.foreground
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    Label {
                        text: panel.standing(row.modelData)
                        visible: text !== ""
                        color: panel.ui.muted
                        font.pixelSize: 12
                    }
                    IconButton {
                        id: personButton
                        visible: panel.moderates && !row.modelData.self
                        implicitWidth: 28
                        implicitHeight: 28
                        iconName: "more"
                        description: qsTr("What to do about %1").arg(row.modelData.nickname)
                        onClicked: {
                            personMenu.person = row.modelData
                            personMenu.toggle(personButton, personButton.width - personMenu.implicitWidth, personButton.height)
                        }
                    }
                }
            }
        }
    }

    // What a moderator, an admin or the owner may do about someone. The
    // server checks too: what it refuses comes back as an error.
    AppMenu {
        id: personMenu
        property var person: ({})
        readonly property bool isModerator: person.role === "moderator"
        readonly property bool isMember: person.affiliation === "member"
        readonly property bool aboveUs: person.affiliation === "owner" || person.affiliation === "admin"

        AppMenuItem {
            visible: !!panel.powers.setRoles && !personMenu.aboveUs
            height: visible ? implicitHeight : 0
            text: personMenu.isModerator ? qsTr("Take moderation") : qsTr("Make moderator")
            onTriggered: backend.setModerator(personMenu.person.nickname, !personMenu.isModerator)
        }
        AppMenuItem {
            visible: !!panel.powers.setAffiliations && !personMenu.aboveUs
            height: visible ? implicitHeight : 0
            text: personMenu.isMember ? qsTr("Take membership") : qsTr("Make member")
            onTriggered: backend.setMember(personMenu.person.nickname, !personMenu.isMember)
        }
        AppMenuSeparator {}
        AppMenuItem {
            visible: !!panel.powers.setRoles && !personMenu.aboveUs
            height: visible ? implicitHeight : 0
            text: qsTr("Remove from the room…")
            destructive: true
            onTriggered: reasonDialog.ask("kick", personMenu.person.nickname)
        }
        AppMenuItem {
            visible: !!panel.powers.setAffiliations && !personMenu.aboveUs
            height: visible ? implicitHeight : 0
            text: qsTr("Ban…")
            destructive: true
            onTriggered: reasonDialog.ask("ban", personMenu.person.nickname)
        }
    }

    // Removing or banning someone, with a reason they see if given.
    AppDialog {
        id: reasonDialog
        property string action
        property string nickname

        function ask(what, who) {
            action = what
            nickname = who
            reasonField.text = ""
            open()
        }

        title: action === "ban" ? qsTr("Ban %1?").arg(nickname) : qsTr("Remove %1 from the room?").arg(nickname)
        subtitle: action === "ban" ? qsTr("They leave and can't come back until the ban is lifted.")
                                   : qsTr("They leave the room, and can join again.")
        acceptText: action === "ban" ? qsTr("Ban") : qsTr("Remove")
        onAboutToShow: reasonField.forceActiveFocus()
        onAccepted: {
            if (action === "ban")
                backend.banParticipant(nickname, reasonField.text)
            else
                backend.kickParticipant(nickname, reasonField.text)
        }

        AppTextField {
            id: reasonField
            Layout.fillWidth: true
            placeholderText: qsTr("Reason (optional, they see it)")
            onAccepted: reasonDialog.accept()
        }
    }

    // Someone into the room, picked from the contacts.
    AppDialog {
        id: inviteDialog
        property var people: []

        title: qsTr("Invite to %1").arg(backend.currentChatName)
        subtitle: qsTr("They get an invitation they can accept or decline.")
        acceptText: qsTr("Invite")
        acceptEnabled: inviteList.currentIndex >= 0
        onAboutToShow: {
            inviteSearch.text = ""
            inviteReason.text = ""
            people = backend.contacts.destinations("").filter(person => !person.isRoom)
            inviteList.currentIndex = people.length > 0 ? 0 : -1
            inviteSearch.forceActiveFocus()
        }
        onAccepted: backend.inviteToRoom(people[inviteList.currentIndex].jid, inviteReason.text)

        AppTextField {
            id: inviteSearch
            Layout.fillWidth: true
            placeholderText: qsTr("Search contacts")
            onTextChanged: {
                inviteDialog.people = backend.contacts.destinations(text).filter(person => !person.isRoom)
                inviteList.currentIndex = inviteDialog.people.length > 0 ? 0 : -1
            }
            Keys.onReturnPressed: inviteDialog.accept()
            Keys.onEnterPressed: inviteDialog.accept()
            Keys.onDownPressed: inviteList.incrementCurrentIndex()
            Keys.onUpPressed: inviteList.decrementCurrentIndex()
        }
        ListView {
            id: inviteList
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            clip: true
            model: inviteDialog.people
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: person
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 44
                radius: Math.min(4, theme.radius)
                color: ListView.isCurrentItem ? panel.ui.hover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 10
                    ContactAvatar {
                        implicitWidth: 28
                        implicitHeight: 28
                        jid: person.modelData.jid
                        name: person.modelData.name
                        source: person.modelData.avatar
                        presence: person.modelData.presence
                        ringColor: person.ListView.isCurrentItem ? panel.ui.hover : panel.ui.panel
                    }
                    Label {
                        Layout.fillWidth: true
                        text: person.modelData.name
                        color: panel.ui.foreground
                        elide: Text.ElideRight
                    }
                }
                TapHandler {
                    onTapped: inviteList.currentIndex = person.index
                    onDoubleTapped: {
                        inviteList.currentIndex = person.index
                        inviteDialog.accept()
                    }
                }
            }
        }
        AppTextField {
            id: inviteReason
            Layout.fillWidth: true
            placeholderText: qsTr("A note with the invitation (optional)")
            onAccepted: inviteDialog.accept()
        }
    }

    // Closing the room for good, for everyone in it.
    AppDialog {
        id: closeDialog
        title: qsTr("Close %1 for good?").arg(backend.currentChatName)
        subtitle: qsTr("The room is destroyed on the server and everyone in it leaves; this can't be undone.")
        acceptText: qsTr("Close the room")
        onAboutToShow: closeReason.text = ""
        onAccepted: backend.destroyRoom(closeReason.text)

        AppTextField {
            id: closeReason
            Layout.fillWidth: true
            placeholderText: qsTr("Reason (optional, they see it)")
            onAccepted: closeDialog.accept()
        }
    }
}
