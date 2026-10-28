import QtQuick
import QtQuick.Controls

// A choice among a few, side by side, in the look of the sidebar's
// Conversations | Contacts: options are {label, value}, and an option can
// be turned off with enabled: false.
Rectangle {
    id: control

    property var options: []
    property var value
    readonly property var ui: theme.colors

    signal picked(var value)

    implicitWidth: 360
    implicitHeight: 32
    radius: Math.min(4, theme.radius)
    color: ui.background
    border.color: ui.border

    Row {
        anchors.fill: parent
        anchors.margins: 3
        Repeater {
            model: control.options
            delegate: Rectangle {
                id: segment
                required property var modelData
                readonly property bool active: control.value === modelData.value
                readonly property bool usable: modelData.enabled !== false
                width: (control.width - 6) / control.options.length
                height: parent.height
                radius: Math.min(3, theme.radius)
                color: active ? control.ui.selection
                     : segmentHover.hovered && usable ? control.ui.hover : "transparent"
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 8
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: segment.modelData.label
                    font.pixelSize: 13
                    font.weight: segment.active ? Font.DemiBold : Font.Normal
                    color: segment.active ? control.ui.selectionText : control.ui.muted
                    opacity: segment.usable ? 1 : 0.45
                }
                HoverHandler { id: segmentHover; cursorShape: segment.usable ? Qt.PointingHandCursor : Qt.ArrowCursor }
                TapHandler {
                    enabled: segment.usable
                    onTapped: {
                        control.value = segment.modelData.value
                        control.picked(segment.modelData.value)
                    }
                }
                Accessible.role: Accessible.RadioButton
                Accessible.name: segment.modelData.label
                Accessible.checked: segment.active
            }
        }
    }
}
