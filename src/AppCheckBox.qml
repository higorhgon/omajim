import QtQuick
import QtQuick.Controls

// A check box in Hype's style: an accent square with a check, the label in
// the foreground color.
CheckBox {
    id: box

    readonly property var ui: theme.colors

    font.pixelSize: 13
    indicator: Rectangle {
        x: box.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        width: 18
        height: 18
        radius: Math.min(3, theme.radius)
        color: box.checked ? box.ui.accent : box.ui.background
        border.color: box.checked || box.visualFocus ? box.ui.accent : box.ui.border
        AppIcon {
            anchors.centerIn: parent
            width: 14
            height: 14
            visible: box.checked
            name: "check"
            color: box.ui.accentText
        }
    }
    contentItem: Label {
        leftPadding: box.indicator.width + 10
        text: box.text
        font: box.font
        color: box.ui.foreground
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }
}
