import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

// The main action of a view, filled with the theme accent. Material's own
// highlighted button takes its text color from the dark theme, not from the
// accent, which leaves white text on light accents like the default yellow.
Button {
    id: control

    Material.roundedScale: theme.radius
    topInset: 6
    bottomInset: 6

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? theme.accentForeground : win.mutedColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitWidth: 64
        implicitHeight: 40
        radius: Math.min(theme.radius, height / 2)
        color: !control.enabled ? Qt.rgba(1, 1, 1, 0.08)
             : control.down ? Qt.darker(theme.accent, 1.15)
             : control.hovered ? Qt.lighter(theme.accent, 1.08)
             : theme.accent
    }
}
