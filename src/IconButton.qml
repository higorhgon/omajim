import QtQuick
import QtQuick.Controls

// A square toolbar button with a line icon, lit on hover, as in Hype's
// toolbar. The description is its tooltip and accessible name.
ToolButton {
    id: button

    required property string iconName
    required property string description
    property bool primary: false
    // The icon's color; white over a picture, for instance.
    property color ink: lit || framed ? ui.foreground : ui.muted
    // A visible button shape at rest, for actions inside content (a file's
    // folder and save buttons), where a bare icon wouldn't read as one.
    property bool framed: false
    // The framed fill; translucent over a picture.
    property color frameColor: ui.button
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
            color: button.primary ? button.ui.accentText : button.ink
        }
    }

    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: button.primary ? (button.lit ? button.ui.accentHover : button.ui.accent)
             : button.lit ? button.ui.hover
             : button.framed ? button.frameColor : "transparent"
        border.color: button.visualFocus ? button.ui.accent
                    : button.framed ? Qt.alpha(button.ui.foreground, 0.18) : "transparent"
    }
}
