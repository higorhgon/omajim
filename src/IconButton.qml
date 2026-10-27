import QtQuick
import QtQuick.Controls

// A square toolbar button with a line icon, lit on hover, as in Hype's
// toolbar. The description is its tooltip and accessible name.
ToolButton {
    id: button

    required property string iconName
    required property string description
    property bool primary: false
    readonly property var ui: theme.colors
    readonly property bool lit: hovered || down || checked

    implicitWidth: 36
    implicitHeight: 36
    padding: 0
    focusPolicy: Qt.TabFocus
    Accessible.name: description
    ToolTip.visible: hovered && description !== ""
    ToolTip.delay: 400
    ToolTip.text: description

    contentItem: Item {
        AppIcon {
            anchors.centerIn: parent
            width: 18
            height: 18
            name: button.iconName
            opacity: button.enabled ? 1 : 0.4
            color: button.primary ? button.ui.accentText : button.lit ? button.ui.foreground : button.ui.muted
        }
    }

    background: Rectangle {
        radius: Math.min(3, theme.radius)
        color: button.primary ? (button.lit ? button.ui.accentHover : button.ui.accent)
             : button.lit ? button.ui.hover : "transparent"
        border.color: button.visualFocus ? button.ui.accent : "transparent"
    }
}
