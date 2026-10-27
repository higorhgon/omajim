import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Your status as a select in Hype's style: the current one with its dot,
// and a list in the panel color below.
ComboBox {
    id: picker

    readonly property var ui: theme.colors
    readonly property var statuses: ["online", "chat", "away", "xa", "dnd", "offline"]

    model: statuses
    currentIndex: Math.max(0, statuses.indexOf(backend.status))
    onActivated: index => backend.setStatus(statuses[index])

    implicitHeight: 36
    leftPadding: 10
    rightPadding: 30
    topPadding: 0
    bottomPadding: 0
    font.pixelSize: 13
    Accessible.name: qsTr("Status: %1").arg(win.presenceLabel(backend.status))

    contentItem: RowLayout {
        spacing: 8
        PresenceDot {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            presence: backend.status
        }
        Label {
            Layout.fillHeight: true
            verticalAlignment: Text.AlignVCenter
            text: win.presenceLabel(backend.status)
            font: picker.font
            color: picker.hovered || picker.popup.visible ? picker.ui.foreground : picker.ui.muted
        }
    }

    indicator: AppIcon {
        x: picker.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        width: 12
        height: 12
        name: "chevron-down"
        color: picker.ui.muted
    }

    background: Rectangle {
        radius: Math.min(3, theme.radius)
        color: picker.hovered || picker.popup.visible ? picker.ui.hover : "transparent"
        border.color: picker.visualFocus ? picker.ui.accent : "transparent"
    }

    delegate: ItemDelegate {
        id: option
        required property string modelData
        required property int index
        width: picker.popup.availableWidth
        height: 32
        // The Basic style's own padding would squeeze the row below its
        // text, pushing it down; the row centers it instead.
        topPadding: 0
        bottomPadding: 0
        leftPadding: 10
        rightPadding: 10
        highlighted: picker.highlightedIndex === index
        contentItem: RowLayout {
            spacing: 10
            PresenceDot {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                presence: option.modelData
            }
            Label {
                Layout.fillWidth: true
                Layout.fillHeight: true
                verticalAlignment: Text.AlignVCenter
                text: win.presenceLabel(option.modelData)
                font.pixelSize: 13
                color: picker.ui.foreground
            }
            AppIcon {
                Layout.preferredWidth: 14
                Layout.preferredHeight: 14
                visible: backend.status === option.modelData
                name: "check"
                color: picker.ui.accent
            }
        }
        background: Rectangle {
            radius: Math.min(4, theme.radius)
            color: option.highlighted ? picker.ui.hover : "transparent"
        }
    }

    popup: Popup {
        y: picker.height + 4
        width: Math.max(picker.width, 200)
        padding: 6
        implicitHeight: contentItem.implicitHeight + 12
        background: Rectangle {
            color: picker.ui.panel
            border.color: picker.ui.border
            radius: theme.radius
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: picker.popup.visible ? picker.delegateModel : null
            currentIndex: picker.highlightedIndex
        }
    }
}
