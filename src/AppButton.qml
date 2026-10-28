import QtQuick
import QtQuick.Controls

// A text button in Hype's style: the accent for the one main action, the
// theme's button color for the rest, and a quiet flat one for inline actions.
Button {
    id: control

    property bool primary: false
    property bool destructive: false
    // The pointer is over it, said by whoever covers it with a click area
    // that keeps hover from reaching it (a message's bubble).
    property bool pointerOver: false
    readonly property var ui: theme.colors
    readonly property bool lit: hovered || pointerOver || down

    implicitHeight: 38
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 14
    font.weight: primary ? Font.DemiBold : Font.Normal

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? control.ui.muted
             : control.primary ? control.ui.accentText
             : control.destructive ? control.ui.error
             : control.ui.foreground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 84
        radius: Math.min(4, theme.radius)
        color: control.primary ? (!control.enabled ? control.ui.border
                                  : control.lit ? control.ui.accentHover : control.ui.accent)
             : control.lit ? control.ui.hover
             : control.flat ? "transparent" : control.ui.button
        border.color: control.visualFocus ? (control.primary ? control.ui.foreground : control.ui.accent) : "transparent"
    }
}
