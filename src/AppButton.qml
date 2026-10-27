import QtQuick
import QtQuick.Controls

// A text button in Hype's style: the accent for the one main action, the
// theme's button color for the rest, and a quiet flat one for inline actions.
Button {
    id: control

    property bool primary: false
    property bool destructive: false
    readonly property var ui: theme.colors

    implicitHeight: 38
    leftPadding: 16
    rightPadding: 16
    font.pixelSize: 14
    font.bold: primary

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
                                  : control.hovered || control.down ? control.ui.accentHover : control.ui.accent)
             : control.hovered || control.down ? control.ui.hover
             : control.flat ? "transparent" : control.ui.button
        border.color: control.visualFocus ? (control.primary ? control.ui.foreground : control.ui.accent) : "transparent"
    }
}
