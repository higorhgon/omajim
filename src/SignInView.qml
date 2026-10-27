import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: view
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    function submit() {
        backend.signIn(jidField.text, passwordField.text, serverField.text, rememberBox.checked)
        passwordField.text = ""
    }

    onVisibleChanged: if (visible) (jidField.text === "" ? jidField : passwordField).forceActiveFocus()

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(400, parent.width - 32)
        height: form.implicitHeight + 56
        radius: theme.radius
        color: win.panelColor
        border.color: win.lineColor

        ColumnLayout {
            id: form
            anchors.fill: parent
            anchors.margins: 28
            spacing: 14

            Label {
                text: "Omajim"
                font.pixelSize: 26
                font.weight: Font.DemiBold
                color: win.textColor
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Sign in to your XMPP account.")
                color: win.mutedColor
                wrapMode: Text.Wrap
            }
            TextField {
                Material.roundedScale: Math.min(4, theme.radius)
                id: jidField
                Layout.fillWidth: true
                text: backend.jid
                placeholderText: qsTr("Address (name@server)")
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                onAccepted: passwordField.forceActiveFocus()
            }
            TextField {
                Material.roundedScale: Math.min(4, theme.radius)
                id: passwordField
                Layout.fillWidth: true
                placeholderText: qsTr("Password")
                echoMode: TextInput.Password
                onAccepted: view.submit()
            }
            TextField {
                Material.roundedScale: Math.min(4, theme.radius)
                id: serverField
                Layout.fillWidth: true
                text: backend.host
                placeholderText: qsTr("Server (optional, host:port)")
                inputMethodHints: Qt.ImhNoAutoUppercase
                onAccepted: view.submit()
            }
            CheckBox {
                id: rememberBox
                Layout.leftMargin: -8
                text: qsTr("Remember the password in the keyring")
                checked: true
            }
            AccentButton {
                Layout.fillWidth: true
                text: qsTr("Sign in")
                enabled: jidField.text.indexOf("@") > 0 && passwordField.text !== ""
                onClicked: view.submit()
            }
        }
    }
}
