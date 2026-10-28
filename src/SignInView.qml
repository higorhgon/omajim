import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Signing in, as a card in the middle of the window, laid out like Hype's
// dialogs: a title, a muted line, the fields, and the accent action. Also
// for adding another account, which starts empty and can be called off.
Item {
    id: view

    readonly property var ui: theme.colors

    function submit() {
        if (!signInButton.enabled)
            return
        backend.signIn(jidField.text, passwordField.text, serverField.text, rememberBox.checked)
        passwordField.text = ""
        // As the account had it; a new one keeps it.
        rememberBox.checked = backend.jid === "" || backend.savePassword
    }

    // The account in use, or nothing for a new one.
    function fill() {
        jidField.text = backend.jid
        serverField.text = backend.host
        passwordField.text = ""
        if (jidField.text === "")
            jidField.forceActiveFocus()
        else
            passwordField.forceActiveFocus()
    }

    onVisibleChanged: if (visible) fill()
    Connections {
        target: backend
        function onSignedInChanged() { if (view.visible) view.fill() }
    }
    Connections {
        target: accounts
        function onAddingChanged() { if (view.visible) view.fill() }
        function onCurrentChanged() { if (view.visible) view.fill() }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 48)
        height: form.implicitHeight + 48
        radius: theme.radius
        color: view.ui.panel
        border.color: view.ui.border

        ColumnLayout {
            id: form
            anchors.fill: parent
            anchors.margins: 24
            spacing: 18

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: accounts.adding ? qsTr("Add an account") : qsTr("Sign in")
                    color: view.ui.foreground
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: accounts.adding ? qsTr("Sign in to another XMPP account. To switch accounts, click your address at the top of the window.")
                                                : qsTr("Sign in to your XMPP account.")
                    color: view.ui.muted
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                AppTextField {
                    id: jidField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Address (name@server)")
                    inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                    onAccepted: passwordField.forceActiveFocus()
                }
                AppTextField {
                    id: passwordField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Password")
                    echoMode: TextInput.Password
                    onAccepted: view.submit()
                }
                AppTextField {
                    id: serverField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Server (optional, host:port)")
                    inputMethodHints: Qt.ImhNoAutoUppercase
                    onAccepted: view.submit()
                }
                AppCheckBox {
                    id: rememberBox
                    Layout.leftMargin: -6
                    checked: true
                    text: qsTr("Remember the password in the keyring")
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    visible: accounts.adding
                    text: qsTr("Cancel")
                    onClicked: accounts.cancelAdding()
                }
                AppButton {
                    id: signInButton
                    primary: true
                    text: qsTr("Sign in")
                    enabled: jidField.text.indexOf("@") > 0 && passwordField.text !== ""
                    onClicked: view.submit()
                }
            }
        }
    }
}
