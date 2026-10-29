import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A new smiley of ours: a picture and the text it stands for.
AppDialog {
    id: smileyDialog
    property string picture: ""
    property var preset: null

    // Opened empty, or with a picture and a text to keep (a smiley someone
    // sent), the text still to change.
    function offer(path, shortcut) {
        preset = {picture: path, text: shortcut}
        open()
    }

    title: qsTr("New smiley")
    subtitle: qsTr("Write its text in a message and Pidgin users see the picture; others see the text.")
    acceptText: qsTr("Add")
    acceptEnabled: picture !== "" && shortcutField.text.trim() !== ""
    closeOnAccept: false
    onAboutToShow: {
        picture = preset ? preset.picture : ""
        shortcutField.text = preset ? preset.text : ""
        preset = null
        smileyError.text = ""
        shortcutField.forceActiveFocus()
        shortcutField.selectAll()
    }
    onAccepted: {
        smileyError.text = backend.smileys.add(shortcutField.text, picture)
        if (smileyError.text === "")
            close()
    }

    Connections {
        target: backend
        function onSmileyPicturePicked(path) { smileyDialog.picture = path }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12
        Rectangle {
            implicitWidth: 56
            implicitHeight: 56
            radius: theme.radius
            color: smileyDialog.ui.background
            border.color: smileyDialog.ui.border
            AnimatedImage {
                anchors.centerIn: parent
                width: 44
                height: 44
                source: smileyDialog.picture === "" ? "" : "file://" + smileyDialog.picture
                fillMode: Image.PreserveAspectFit
            }
        }
        AppButton {
            text: smileyDialog.picture === "" ? qsTr("Choose a picture…") : qsTr("Another picture…")
            onClicked: backend.chooseSmileyPicture()
        }
    }
    AppTextField {
        id: shortcutField
        Layout.fillWidth: true
        Layout.topMargin: 12
        placeholderText: qsTr("Its text, like :party:")
        onAccepted: smileyDialog.accept()
    }
    Label {
        id: smileyError
        Layout.fillWidth: true
        Layout.topMargin: 8
        visible: text !== ""
        color: smileyDialog.ui.error
        wrapMode: Text.Wrap
    }
}
