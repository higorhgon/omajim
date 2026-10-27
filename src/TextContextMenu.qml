import QtQuick
import QtQuick.Controls

// The right-click menu of a text field, in the app's style instead of the
// one Qt gives text controls. A field can take over Paste (the message field
// offers to send pictures and files) with pasteHandler, which returns true
// when it did.
AppMenu {
    id: menu

    required property Item target
    property var pasteHandler: null
    readonly property bool editable: !target.readOnly

    AppMenuItem {
        text: qsTr("Undo")
        hint: "Ctrl+Z"
        visible: menu.editable
        height: visible ? implicitHeight : 0
        enabled: menu.target.canUndo
        onTriggered: menu.target.undo()
    }
    AppMenuItem {
        text: qsTr("Redo")
        hint: "Ctrl+Shift+Z"
        visible: menu.editable
        height: visible ? implicitHeight : 0
        enabled: menu.target.canRedo
        onTriggered: menu.target.redo()
    }
    AppMenuSeparator {
        visible: menu.editable
        height: visible ? implicitHeight : 0
    }
    AppMenuItem {
        text: qsTr("Cut")
        hint: "Ctrl+X"
        visible: menu.editable
        height: visible ? implicitHeight : 0
        enabled: menu.target.selectedText !== ""
        onTriggered: menu.target.cut()
    }
    AppMenuItem {
        text: qsTr("Copy")
        hint: "Ctrl+C"
        enabled: menu.target.selectedText !== ""
        onTriggered: menu.target.copy()
    }
    AppMenuItem {
        text: qsTr("Paste")
        hint: "Ctrl+V"
        visible: menu.editable
        height: visible ? implicitHeight : 0
        enabled: menu.target.canPaste || menu.pasteHandler !== null
        onTriggered: {
            if (!(menu.pasteHandler && menu.pasteHandler()))
                menu.target.paste()
        }
    }
    AppMenuSeparator {}
    AppMenuItem {
        text: qsTr("Select all")
        hint: "Ctrl+A"
        enabled: menu.target.length > 0
        onTriggered: menu.target.selectAll()
    }
}
