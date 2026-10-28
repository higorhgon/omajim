import QtQuick
import QtQuick.Controls

MenuItem {
    id: item

    // A shortcut shown at the right edge, like "Ctrl+N".
    property string hint
    property bool destructive: false
    // A status (online, away…): its dot goes before the label.
    property string presence
    // A color, shown as a square before the label.
    property string swatch
    readonly property var ui: theme.colors
    readonly property color ink: destructive ? ui.error : ui.foreground

    implicitHeight: 32
    implicitWidth: leftPadding + (presence || swatch ? 18 : 0) + label.implicitWidth + (hint ? 28 + hintLabel.implicitWidth : 0)
                   + (subMenu ? 28 : 0) + rightPadding
    leftPadding: menu && menu.hasChecks ? 32 : 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: 13

    indicator: AppIcon {
        x: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        visible: item.checkable && item.checked
        name: "check"
        color: item.ui.accent
    }
    arrow: AppIcon {
        x: parent.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        visible: !!item.subMenu
        name: "chevron-right"
        color: item.ui.muted
    }
    contentItem: Item {
        opacity: item.enabled ? 1 : 0.4
        PresenceDot {
            id: dot
            visible: item.presence !== ""
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            presence: item.presence || "offline"
        }
        Rectangle {
            id: swatchBox
            visible: item.swatch !== ""
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            height: 12
            radius: Math.min(2, theme.radius)
            color: item.swatch || "transparent"
            border.color: item.ui.border
        }
        Label {
            id: label
            anchors.left: parent.left
            anchors.leftMargin: dot.visible || swatchBox.visible ? 18 : 0
            anchors.verticalCenter: parent.verticalCenter
            text: item.text
            font: item.font
            color: item.ink
        }
        Label {
            id: hintLabel
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: item.hint
            font.pixelSize: 12
            color: item.ui.muted
        }
    }
    background: Rectangle {
        radius: Math.min(4, theme.radius)
        color: item.highlighted && item.enabled ? item.ui.hover : "transparent"
    }
}
