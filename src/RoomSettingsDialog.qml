import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A room's settings: to create one on the server's room service, or to
// change one we own (its subject too, if we may).
AppDialog {
    id: dialog

    // True to create a room; false to change the open one.
    property bool creating: true
    property bool canConfigure: true
    property bool canChangeSubject: false
    // The address follows the name until it's typed by hand.
    property bool addressTouched: false

    function openToCreate() {
        creating = true
        canConfigure = true
        canChangeSubject = false
        addressTouched = false
        nameField.text = ""
        addressField.text = ""
        descriptionField.text = ""
        subjectField.text = ""
        access.value = "open"
        passwordField.text = ""
        listedBox.checked = false
        persistentBox.checked = true
        open()
    }

    // The open room's settings, as backend.roomSettingsRead has them.
    function openToChange(settings, powers) {
        creating = false
        canConfigure = !!powers.configure
        canChangeSubject = !!powers.changeSubject
        nameField.text = settings.name || ""
        descriptionField.text = settings.description || ""
        subjectField.text = settings.subject || ""
        access.value = settings.access || "open"
        passwordField.text = settings.password || ""
        listedBox.checked = !!settings.listed
        persistentBox.checked = !!settings.persistent
        open()
    }

    title: creating ? qsTr("Create a group chat") : qsTr("Group chat settings")
    subtitle: creating ? qsTr("It's created on %1, with you as its owner.").arg(backend.roomService)
                       : (canConfigure ? "" : qsTr("Only the owner changes the rest."))
    acceptText: creating ? qsTr("Create") : qsTr("Save")
    acceptEnabled: nameField.text.trim() !== "" && (!creating || /^[a-z0-9._-]+$/.test(addressField.text))
                   && (access.value !== "password" || passwordField.text !== "")
    width: Math.min(500, parent ? parent.width - 48 : 500)
    onAboutToShow: nameField.forceActiveFocus()
    onAccepted: {
        const settings = {name: nameField.text, address: addressField.text, description: descriptionField.text,
                          subject: subjectField.text, access: access.value, password: passwordField.text,
                          listed: listedBox.checked, persistent: persistentBox.checked}
        if (creating)
            backend.createRoom(settings)
        else
            backend.configureRoom(settings)
    }

    AppTextField {
        id: nameField
        Layout.fillWidth: true
        enabled: dialog.canConfigure
        placeholderText: qsTr("Name, like Suporte TI")
        onTextChanged: if (dialog.creating && !dialog.addressTouched) addressField.text = backend.roomAddressFor(text)
        onAccepted: dialog.accept()
    }
    RowLayout {
        Layout.fillWidth: true
        visible: dialog.creating
        spacing: 6
        AppTextField {
            id: addressField
            Layout.fillWidth: true
            placeholderText: qsTr("address")
            inputMethodHints: Qt.ImhNoAutoUppercase
            onTextEdited: dialog.addressTouched = true
            onAccepted: dialog.accept()
        }
        Label {
            text: "@" + backend.roomService
            color: dialog.ui.muted
            font.pixelSize: 13
        }
    }
    AppTextField {
        id: descriptionField
        Layout.fillWidth: true
        enabled: dialog.canConfigure
        placeholderText: qsTr("Description (optional)")
        onAccepted: dialog.accept()
    }
    AppTextField {
        id: subjectField
        Layout.fillWidth: true
        visible: !dialog.creating && dialog.canChangeSubject
        placeholderText: qsTr("Subject, shown under the room's name")
        onAccepted: dialog.accept()
    }

    Label {
        Layout.topMargin: 6
        text: qsTr("Who can join").toUpperCase()
        font.pixelSize: 11
        font.letterSpacing: 1
        color: dialog.ui.accent
        visible: dialog.canConfigure
    }
    AppSegmented {
        id: access
        Layout.fillWidth: true
        visible: dialog.canConfigure
        value: "open"
        options: [{label: qsTr("Anyone with the address"), value: "open"},
                  {label: qsTr("Members only"), value: "members"},
                  {label: qsTr("With a password"), value: "password"}]
    }
    AppTextField {
        id: passwordField
        Layout.fillWidth: true
        visible: dialog.canConfigure && access.value === "password"
        placeholderText: qsTr("Room password")
        echoMode: TextInput.Password
        onAccepted: dialog.accept()
    }
    AppCheckBox {
        id: listedBox
        Layout.leftMargin: -6
        Layout.fillWidth: true
        visible: dialog.canConfigure
        text: qsTr("List it in the server's rooms")
    }
    AppCheckBox {
        id: persistentBox
        Layout.leftMargin: -6
        Layout.fillWidth: true
        visible: dialog.canConfigure
        text: qsTr("Persistent: it stays when everyone leaves")
    }
    Label {
        Layout.fillWidth: true
        visible: dialog.canConfigure && !persistentBox.checked
        text: qsTr("Without it, the room and its settings are gone once the last person leaves.")
        color: dialog.ui.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }
}
