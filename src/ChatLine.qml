import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// One line of the conversation: a message bubble, or a file with its
// transfer state. A full-width item, since ListView pins delegates to x: 0.
Item {
    id: line
    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    required property var messageId
    required property string body
    required property bool outgoing
    required property var stamp
    required property string kind
    required property string filePath
    required property string fileSize
    required property string transferState
    required property double progress

    width: ListView.view.width
    height: bubble.height

    readonly property string stateLabel: {
        switch (transferState) {
        case "offered": return outgoing ? qsTr("Waiting for them to accept") : qsTr("Wants to send you this file")
        case "active": return qsTr("%1% transferred").arg(Math.round(progress * 100))
        case "done": return outgoing ? qsTr("Sent") : qsTr("Saved")
        case "refused": return outgoing ? qsTr("They declined") : qsTr("Declined")
        case "canceled": return qsTr("Canceled")
        }
        return qsTr("Failed")
    }

    Text {
        id: measure
        visible: false
        text: line.body
        textFormat: Text.PlainText
        font.pixelSize: 14
    }

    Rectangle {
        id: bubble
        x: line.outgoing ? line.width - width - 16 : 16
        // Sized from the text laid out on one line, so short messages get a
        // snug bubble and long ones wrap at the maximum width.
        width: Math.min(line.width * 0.72, line.kind === "file" ? 320 : Math.max(Math.ceil(measure.implicitWidth) + 2, 64) + 24)
        height: content.implicitHeight + 16
        radius: theme.radius
        color: line.outgoing ? Qt.alpha(theme.accent, 0.16) : win.surfaceColor
        border.color: line.outgoing ? Qt.alpha(theme.accent, 0.35) : win.lineColor

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 8
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 4

            TextEdit {
                Layout.fillWidth: true
                visible: line.kind === "text"
                text: line.body
                textFormat: TextEdit.PlainText
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: win.textColor
                selectionColor: theme.accent
                selectedTextColor: theme.accentForeground
                font.pixelSize: 14
            }

            ColumnLayout {
                visible: line.kind === "file"
                Layout.fillWidth: true
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    text: line.body
                    color: win.textColor
                    font.weight: Font.Medium
                    elide: Text.ElideMiddle
                }
                Label {
                    text: "%1 · %2".arg(line.fileSize).arg(line.stateLabel)
                    color: win.mutedColor
                    font.pixelSize: 12
                }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: line.transferState === "active"
                    value: line.progress
                }
                RowLayout {
                    spacing: 4
                    AccentButton {
                        visible: !line.outgoing && line.transferState === "offered"
                        text: qsTr("Accept")
                        onClicked: backend.acceptFile(line.messageId)
                    }
                    Button {
                        Material.roundedScale: theme.radius
                        visible: !line.outgoing && line.transferState === "offered"
                        text: qsTr("Decline")
                        flat: true
                        onClicked: backend.refuseFile(line.messageId)
                    }
                    Button {
                        Material.roundedScale: theme.radius
                        visible: line.transferState === "active" || (line.outgoing && line.transferState === "offered")
                        text: qsTr("Cancel")
                        flat: true
                        onClicked: backend.cancelTransfer(line.messageId)
                    }
                    Button {
                        Material.roundedScale: theme.radius
                        visible: line.transferState === "done" && line.filePath !== ""
                        text: qsTr("Open")
                        flat: true
                        onClicked: backend.openFile(line.filePath)
                    }
                }
            }

            Label {
                Layout.alignment: line.outgoing ? Qt.AlignRight : Qt.AlignLeft
                // Today's lines show the time only; older ones the date too.
                // Reading translator.language re-renders on a language switch.
                text: {
                    translator.language
                    const today = new Date().toDateString() === line.stamp.toDateString()
                    return today ? line.stamp.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                                 : line.stamp.toLocaleString(Qt.locale(), Locale.ShortFormat)
                }
                color: win.mutedColor
                font.pixelSize: 11
            }
        }
    }
}
