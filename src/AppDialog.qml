import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A modal dialog in Hype's style: a title, a muted line under it, the
// fields, and Cancel beside the accent action. Enter in a field accepts.
Popup {
    id: dialog

    property string title
    property string subtitle
    property string acceptText: qsTr("OK")
    property bool acceptEnabled: true
    // False keeps the dialog open, to show the outcome of what it did.
    property bool closeOnAccept: true
    property bool showCancel: true
    default property alias body: bodyColumn.data
    readonly property var ui: theme.colors

    signal accepted()

    function accept() {
        if (!acceptEnabled)
            return
        accepted()
        if (closeOnAccept)
            close()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 24
    width: Math.min(440, parent ? parent.width - 48 : 440)
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: dialog.ui.panel
        border.color: dialog.ui.border
        radius: theme.radius
    }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.45) }

    // Spacing is set per block, so an empty body leaves no gap.
    contentItem: ColumnLayout {
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: dialog.title
                color: dialog.ui.foreground
                font.pixelSize: 20
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                visible: text !== ""
                text: dialog.subtitle
                color: dialog.ui.muted
                font.pixelSize: 14
                wrapMode: Text.Wrap
            }
        }

        ColumnLayout {
            id: bodyColumn
            Layout.fillWidth: true
            // Room above only with something in it. (Hiding an empty body
            // instead would hide its children for good, as a child of a
            // hidden item never counts as visible.)
            Layout.topMargin: implicitHeight > 0 ? 18 : 0
            spacing: 10
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 18
            spacing: 10
            Item { Layout.fillWidth: true }
            AppButton {
                visible: dialog.showCancel
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }
            AppButton {
                primary: true
                text: dialog.acceptText
                enabled: dialog.acceptEnabled
                onClicked: dialog.accept()
            }
        }
    }
}
