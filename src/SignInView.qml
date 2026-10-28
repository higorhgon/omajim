import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Signing in, as a card in the middle of the window, laid out like Hype's
// dialogs: a title, a muted line, the fields, and the accent action.
Item {
    id: view

    readonly property var ui: theme.colors

    function submit() {
        if (!signInButton.enabled)
            return
        backend.signIn(jidField.text, passwordField.text, serverField.text, rememberBox.checked)
        passwordField.text = ""
    }

    onVisibleChanged: if (visible) (jidField.text === "" ? jidField : passwordField).forceActiveFocus()

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
                    text: qsTr("Sign in")
                    color: view.ui.foreground
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Sign in to your XMPP account.")
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
                    text: backend.jid
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
                    text: backend.host
                    placeholderText: qsTr("Server (optional, host:port)")
                    inputMethodHints: Qt.ImhNoAutoUppercase
                    onAccepted: view.submit()
                }
                CheckBox {
                    id: rememberBox
                    Layout.leftMargin: -6
                    checked: true
                    text: qsTr("Remember the password in the keyring")
                    font.pixelSize: 13
                    indicator: Rectangle {
                        x: rememberBox.leftPadding
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18
                        height: 18
                        radius: Math.min(3, theme.radius)
                        color: rememberBox.checked ? view.ui.accent : view.ui.background
                        border.color: rememberBox.checked ? view.ui.accent
                                    : rememberBox.visualFocus ? view.ui.accent : view.ui.border
                        AppIcon {
                            anchors.centerIn: parent
                            width: 14
                            height: 14
                            visible: rememberBox.checked
                            name: "check"
                            color: view.ui.accentText
                        }
                    }
                    contentItem: Label {
                        leftPadding: rememberBox.indicator.width + 10
                        text: rememberBox.text
                        font: rememberBox.font
                        color: view.ui.foreground
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
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
