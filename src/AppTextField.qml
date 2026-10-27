import QtQuick
import QtQuick.Controls

// A text field in Hype's style: the theme's background inside a border that
// takes the accent while focused.
TextField {
    id: field

    readonly property var ui: theme.colors

    implicitHeight: 40
    leftPadding: 12
    rightPadding: 12
    font.pixelSize: 14
    color: ui.foreground
    placeholderTextColor: ui.muted
    selectionColor: ui.selection
    selectedTextColor: ui.selectionText
    ContextMenu.menu: TextContextMenu { target: field }

    background: Rectangle {
        color: field.ui.background
        radius: Math.min(4, theme.radius)
        border.color: field.activeFocus ? field.ui.accent : field.ui.border
    }
}
