import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Pidgin's Accounts window: every account, the box that turns it on or off
// (all that are on stay connected at once), and adding, setting up or
// removing one.
AppDialog {
    id: dialog

    property string selected: ""
    readonly property var chosen: accounts.list.find(account => account.jid === selected) || null
    readonly property int enabledCount: accounts.list.filter(account => account.enabled).length

    function stateOf(account) {
        if (!account.enabled)
            return qsTr("Off")
        if (!account.signedIn)
            return qsTr("Needs the password")
        switch (account.connection) {
        case "online": return qsTr("Connected")
        case "connecting": return qsTr("Connecting…")
        }
        return qsTr("Disconnected")
    }

    title: qsTr("Accounts")
    subtitle: qsTr("Every account that's on stays connected at the same time.")
    width: Math.min(560, parent ? parent.width - 48 : 560)
    acceptText: qsTr("Close")
    showCancel: false
    onAboutToShow: {
        if (!chosen)
            selected = accounts.list.length > 0 ? accounts.list[0].jid : ""
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Math.min(280, list.implicitHeight + 2)
        radius: Math.min(4, theme.radius)
        color: dialog.ui.background
        border.color: dialog.ui.border

        Flickable {
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            contentHeight: list.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: list
                width: parent.width
                spacing: 0
                Repeater {
                    model: accounts.list
                    ItemDelegate {
                        id: row
                        required property var modelData
                        readonly property bool picked: modelData.jid === dialog.selected
                        Layout.fillWidth: true
                        implicitHeight: 52
                        leftPadding: 12
                        rightPadding: 12
                        focusPolicy: Qt.NoFocus
                        onClicked: dialog.selected = modelData.jid
                        onDoubleClicked: settingsDialog.openFor(modelData)
                        background: Rectangle {
                            color: row.picked ? dialog.ui.selection : row.hovered ? dialog.ui.hover : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 10
                            // On or off; the last one on stays on.
                            AppCheckBox {
                                Layout.alignment: Qt.AlignVCenter
                                checkable: false
                                checked: row.modelData.enabled
                                enabled: !(row.modelData.enabled && dialog.enabledCount <= 1)
                                onClicked: accounts.setEnabled(row.modelData.jid, !row.modelData.enabled)
                                ToolTip.visible: hovered
                                ToolTip.delay: 500
                                ToolTip.text: row.modelData.enabled ? qsTr("Turn this account off") : qsTr("Turn this account on")
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.jid
                                    elide: Text.ElideRight
                                    color: row.picked ? dialog.ui.selectionText : dialog.ui.foreground
                                    font.pixelSize: 14
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: dialog.stateOf(row.modelData)
                                    elide: Text.ElideRight
                                    color: row.picked ? dialog.ui.selectionText : dialog.ui.muted
                                    font.pixelSize: 12
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        AppButton {
            text: qsTr("Add…")
            enabled: !accounts.adding
            onClicked: {
                dialog.close()
                accounts.addAccount()
            }
        }
        AppButton {
            text: qsTr("Set up…")
            enabled: dialog.chosen !== null
            onClicked: settingsDialog.openFor(dialog.chosen)
        }
        AppButton {
            text: qsTr("Remove…")
            destructive: true
            enabled: dialog.chosen !== null
            onClicked: {
                removeDialog.jid = dialog.selected
                removeDialog.open()
            }
        }
        Item { Layout.fillWidth: true }
    }

    // An account's settings.
    AppDialog {
        id: settingsDialog
        property string jid

        function openFor(account) {
            jid = account.jid
            hostField.text = account.host
            saveBox.checked = account.savePassword
            open()
        }

        title: qsTr("Account settings")
        subtitle: jid
        acceptText: qsTr("Save")
        onAccepted: accounts.configure(jid, hostField.text, saveBox.checked)

        AppTextField {
            id: hostField
            Layout.fillWidth: true
            placeholderText: qsTr("Server (optional, host:port)")
            inputMethodHints: Qt.ImhNoAutoUppercase
            onAccepted: settingsDialog.accept()
        }
        AppCheckBox {
            id: saveBox
            Layout.leftMargin: -6
            Layout.fillWidth: true
            text: qsTr("Save the password in the keyring")
        }
        Label {
            Layout.fillWidth: true
            text: saveBox.checked ? qsTr("Omajim signs in by itself.")
                                  : qsTr("Omajim asks for the password when it opens, and when the account is turned back on.")
            color: dialog.ui.muted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
    }

    // Removing an account is for good: it asks first.
    AppDialog {
        id: removeDialog
        property string jid
        title: qsTr("Remove %1?").arg(jid)
        subtitle: accounts.list.length > 1
                  ? qsTr("Its password leaves the keyring and it leaves the list; its conversations stay on this computer.")
                  : qsTr("Its password leaves the keyring and Omajim asks to sign in again; its conversations stay on this computer.")
        acceptText: qsTr("Remove")
        onAccepted: {
            accounts.signOut(jid)
            dialog.selected = accounts.list.length > 0 ? accounts.list[0].jid : ""
            if (accounts.list.length === 0)
                dialog.close()
        }
    }
}
