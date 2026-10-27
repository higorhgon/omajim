import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One line of the conversation: a message bubble, or a file with its
// transfer state. A full-width item, since ListView pins delegates to x: 0.
Item {
    id: line

    required property int index
    required property var messageId
    required property string body
    required property bool outgoing
    required property var stamp
    required property string kind
    required property string filePath
    required property string fileSize
    required property string transferState
    required property double progress
    required property bool delivered
    required property string sender
    required property string url
    required property bool edited
    required property string preview
    required property string route
    required property bool selected
    required property var reply
    required property var reactions

    readonly property var ui: theme.colors

    width: ListView.view.width
    height: bubble.height

    readonly property string stateLabel: {
        switch (transferState) {
        case "offered": return outgoing ? qsTr("Waiting for them to accept") : qsTr("Wants to send you this file")
        case "active": return qsTr("%1% transferred").arg(Math.round(progress * 100))
        case "uploading": return qsTr("%1% uploaded").arg(Math.round(progress * 100))
        case "downloading": return qsTr("%1% downloaded").arg(Math.round(progress * 100))
        case "remote": return qsTr("On the server")
        case "done": return outgoing ? qsTr("Sent") : qsTr("Saved")
        case "refused": return outgoing ? qsTr("They declined") : qsTr("Declined")
        case "canceled": return qsTr("Canceled")
        }
        return qsTr("Failed")
    }
    readonly property bool busy: ["active", "uploading", "downloading"].indexOf(transferState) >= 0
    // The way a direct transfer goes, as the Python Omajim showed it.
    readonly property string routeLabel: {
        switch (route) {
        case "direct": return qsTr("direct connection")
        case "socks": return qsTr("SOCKS5 connection")
        case "proxy-ufw": return qsTr("through the server's proxy, port blocked by ufw")
        case "ibb": return qsTr("through the server (IBB)")
        case "ibb-ufw": return qsTr("through the server (IBB), port blocked by ufw")
        }
        return ""
    }

    Text {
        id: senderMeasure
        visible: false
        text: line.outgoing ? "" : line.sender
        font.pixelSize: 12
        font.bold: true
    }

    // Measured as it will show, styling included: code is wider than the
    // plain text with its markers.
    Text {
        id: measure
        visible: false
        text: line.kind === "text" ? bodyText.text : line.body
        textFormat: Text.RichText
        font.pixelSize: 14
    }

    Rectangle {
        id: bubble
        x: line.outgoing ? line.width - width - 20 : 20
        // Sized from the text laid out on one line, so short messages get a
        // snug bubble and long ones wrap at the maximum width.
        width: Math.min(line.width * 0.72, line.kind === "file" ? 340
                        : Math.max(Math.ceil(measure.implicitWidth) + 2, Math.ceil(senderMeasure.implicitWidth) + 2,
                                   line.reply && line.reply.id ? 220 : 64, reactionRow.implicitWidth,
                                   Math.ceil(stampRow.implicitWidth) + 2) + 28)
        height: content.implicitHeight + 20
        radius: theme.radius
        // Ours in a tint of the accent, theirs on the theme's button color.
        color: line.selected ? Qt.tint(line.ui.background, Qt.alpha(line.ui.accent, 0.3))
             : line.outgoing ? Qt.tint(line.ui.background, Qt.alpha(line.ui.accent, 0.16)) : line.ui.button
        border.color: line.selected ? line.ui.accent : line.outgoing ? Qt.alpha(line.ui.accent, 0.4) : "transparent"
        border.width: line.selected ? 2 : 1

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 10
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 4

            // In a group chat, who wrote it, in a color that stays theirs.
            Label {
                visible: !line.outgoing && line.sender !== ""
                text: line.sender
                font.pixelSize: 12
                font.bold: true
                color: {
                    let hash = 0
                    for (let i = 0; i < line.sender.length; ++i)
                        hash = (hash * 31 + line.sender.charCodeAt(i)) >>> 0
                    return Qt.hsla((hash % 360) / 360, 0.6, 0.68, 1)
                }
            }

            // The line this one answers; a click scrolls to it.
            Rectangle {
                Layout.fillWidth: true
                visible: !!(line.reply && line.reply.id)
                implicitHeight: quoteColumn.implicitHeight + 12
                radius: Math.min(4, theme.radius)
                color: Qt.alpha(line.ui.background, 0.6)

                Rectangle {
                    width: 3
                    height: parent.height
                    color: line.ui.accent
                }
                ColumnLayout {
                    id: quoteColumn
                    anchors.fill: parent
                    anchors.margins: 6
                    anchors.leftMargin: 12
                    spacing: 1
                    Label {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: line.reply ? line.reply.name || "" : ""
                        color: line.ui.accent
                        font.pixelSize: 12
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Label {
                        Layout.fillWidth: true
                        text: line.reply ? line.reply.text || "" : ""
                        color: line.ui.muted
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                    }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        const row = backend.messages.rowOfMessage(line.reply.id)
                        if (row >= 0)
                            line.ListView.view.positionViewAtIndex(row, ListView.Center)
                    }
                }
            }

            // Escaped and linkified in C++; a link opens in the browser.
            TextEdit {
                id: bodyText
                Layout.fillWidth: true
                visible: line.kind === "text"
                text: backend.formatMessage(line.body, line.ui.accent, line.ui.muted)
                textFormat: TextEdit.RichText
                readOnly: true
                // Selection is the conversation's, so it can run across
                // messages; the pointer below drives it.
                selectByMouse: false
                persistentSelection: true
                wrapMode: TextEdit.Wrap
                color: line.ui.foreground
                selectionColor: line.ui.selection
                selectedTextColor: line.ui.selectionText
                font.pixelSize: 14

                HoverHandler {
                    cursorShape: bodyText.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
                }
            }

            // A picture shared as a file shows itself; a click opens it.
            Image {
                id: picture
                visible: line.preview !== "" && status === Image.Ready
                Layout.preferredWidth: Math.min(implicitWidth, bubble.width - 28)
                Layout.preferredHeight: implicitWidth > 0 ? Layout.preferredWidth * implicitHeight / implicitWidth : 0
                Layout.maximumHeight: 260
                source: line.preview
                sourceSize.width: 600
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft
                asynchronous: true
                smooth: true
                mipmap: true

                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: backend.openFile(line.transferState === "done" && line.filePath !== ""
                                               ? line.filePath : decodeURIComponent(line.preview.replace("file://", "")))
                }
            }

            RowLayout {
                visible: line.kind === "file"
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 40
                    implicitHeight: 40
                    radius: Math.min(4, theme.radius)
                    color: line.ui.background
                    border.color: line.ui.border
                    AppIcon {
                        anchors.centerIn: parent
                        width: 20
                        height: 20
                        name: "file"
                        color: line.ui.accent
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        text: line.body
                        color: line.ui.foreground
                        font.bold: true
                        elide: Text.ElideMiddle
                    }
                    Label {
                        Layout.fillWidth: true
                        text: [line.fileSize, line.stateLabel,
                               ["offered", "active", "done"].indexOf(line.transferState) >= 0 ? line.routeLabel : ""]
                              .filter(part => part !== "").join(" · ")
                        wrapMode: Text.Wrap
                        color: line.transferState === "failed" ? line.ui.error : line.ui.muted
                        font.pixelSize: 12
                    }
                    // A thin bar in the accent, like Hype's export progress.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 2
                        visible: line.busy
                        implicitHeight: 3
                        radius: 1.5
                        color: line.ui.border
                        Rectangle {
                            width: parent.width * line.progress
                            height: parent.height
                            radius: parent.radius
                            color: line.ui.accent
                        }
                    }
                    RowLayout {
                        Layout.topMargin: 4
                        spacing: 8
                        // A file on the server comes down only when asked for.
                        AppButton {
                            visible: !line.outgoing && line.url !== ""
                                     && ["remote", "failed", "canceled"].indexOf(line.transferState) >= 0
                            primary: true
                            implicitHeight: 32
                            text: line.transferState === "remote" ? qsTr("Download") : qsTr("Try again")
                            onClicked: backend.downloadFile(line.messageId)
                        }
                        AppButton {
                            visible: !line.outgoing && line.transferState === "offered"
                            primary: true
                            implicitHeight: 32
                            text: qsTr("Accept")
                            onClicked: backend.acceptFile(line.messageId)
                        }
                        AppButton {
                            visible: !line.outgoing && line.transferState === "offered"
                            flat: true
                            implicitHeight: 32
                            text: qsTr("Decline")
                            onClicked: backend.refuseFile(line.messageId)
                        }
                        AppButton {
                            visible: line.busy || (line.outgoing && line.transferState === "offered")
                            implicitHeight: 32
                            text: qsTr("Cancel")
                            onClicked: backend.cancelTransfer(line.messageId)
                        }
                        AppButton {
                            visible: line.transferState === "done" && line.filePath !== ""
                            implicitHeight: 32
                            text: qsTr("Open")
                            onClicked: backend.openFile(line.filePath)
                        }
                    }
                }
            }

            // Reactions, one chip per emoji; a click adds or takes back ours.
            Flow {
                id: reactionRow
                Layout.fillWidth: true
                visible: line.reactions && line.reactions.length > 0
                spacing: 6
                Repeater {
                    model: line.reactions || []
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        implicitWidth: chipLabel.implicitWidth + 16
                        implicitHeight: 24
                        radius: 12
                        color: modelData.mine ? Qt.alpha(line.ui.accent, 0.25) : line.ui.background
                        border.color: modelData.mine ? line.ui.accent : line.ui.border
                        Label {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: "%1 %2".arg(chip.modelData.emoji).arg(chip.modelData.count)
                            color: line.ui.foreground
                            font.pixelSize: 12
                        }
                        HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                        ToolTip.visible: chipHover.hovered
                        ToolTip.text: chip.modelData.who
                        TapHandler { onTapped: backend.toggleReaction(line.messageId, chip.modelData.emoji) }
                    }
                }
            }

            // When it was written, and whether it was edited or delivered.
            RowLayout {
                id: stampRow
                Layout.alignment: line.outgoing ? Qt.AlignRight : Qt.AlignLeft
                spacing: 4

                Label {
                    // Today's lines show the time only; older ones the date too.
                    // Reading translator.language re-renders on a language switch.
                    text: {
                        translator.language
                        const today = new Date().toDateString() === line.stamp.toDateString()
                        return today ? line.stamp.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                                     : line.stamp.toLocaleString(Qt.locale(), Locale.ShortFormat)
                    }
                    color: line.ui.muted
                    font.pixelSize: 11
                }
                Label {
                    visible: line.edited
                    text: qsTr("(edited)")
                    color: line.ui.muted
                    font.pixelSize: 11
                    font.italic: true
                }
                // The other side's client confirmed it arrived (XEP-0184).
                AppIcon {
                    visible: line.outgoing && line.delivered
                    implicitWidth: 13
                    implicitHeight: 13
                    name: "check"
                    color: line.ui.accent
                    ToolTip.visible: deliveredHover.hovered
                    ToolTip.text: qsTr("Delivered")
                    HoverHandler { id: deliveredHover }
                }
            }
        }
    }

    // Shift+click picks whole messages: from the last one clicked to this,
    // or, on one already picked, just leaves it out. A plain click goes on
    // to the text and buttons underneath and starts a new range.
    // Where a point of this line falls in its text: a character position,
    // the start above the text and the end below it. Lines without text
    // count as wholly inside a selection that runs across them.
    function textPosition(x, y) {
        if (line.kind !== "text")
            return 0
        const point = line.mapToItem(bodyText, x, y)
        if (point.y < 0)
            return 0
        if (point.y > bodyText.height)
            return bodyText.length
        return bodyText.positionAt(point.x, point.y)
    }

    // The plain text between two positions; end -1 runs to the end.
    function textBetween(start, end) {
        return line.kind === "text" ? bodyText.getText(start, end < 0 ? bodyText.length : end) : ""
    }

    function syncTextSelection() {
        const view = line.ListView.view
        if (!view || !view.textStart || line.kind !== "text") {
            bodyText.deselect()
            return
        }
        const range = view.orderedTextSelection()
        if (line.index < range.first.row || line.index > range.last.row) {
            bodyText.deselect()
            return
        }
        bodyText.select(line.index === range.first.row ? range.first.pos : 0,
                        line.index === range.last.row ? range.last.pos : bodyText.length)
    }

    Component.onCompleted: syncTextSelection()
    Connections {
        target: line.ListView.view
        function onTextSelectionChanged() { line.syncTextSelection() }
    }

    MouseArea {
        id: pointer
        anchors.fill: bubble
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Otherwise the list takes the drag over after a few pixels to scroll.
        preventStealing: true
        property point pressedAt
        property bool dragged: false

        onPressed: mouse => {
            if (mouse.button === Qt.RightButton) {
                lineMenu.popup()
                mouse.accepted = true
                return
            }
            const view = line.ListView.view
            // Shift+click picks whole messages: from the last one clicked to
            // this, or, on one already picked, just leaves it out.
            if (mouse.modifiers & Qt.ShiftModifier) {
                if (line.selected)
                    backend.messages.toggleSelected(line.messageId)
                else
                    backend.messages.selectRange(view.selectionAnchor >= 0 ? view.selectionAnchor : line.messageId,
                                                 line.messageId)
                view.selectionAnchor = line.messageId
                mouse.accepted = true
                return
            }
            view.selectionAnchor = line.messageId
            backend.messages.clearSelection()
            // On the text: the start of a selection that may run on into
            // other messages, as in ZapFast.
            const inText = line.kind === "text" && bodyText.contains(mapToItem(bodyText, mouse.x, mouse.y))
            if (!inText) {
                // Buttons, files, quotes: theirs to handle.
                view.clearTextSelection()
                mouse.accepted = false
                return
            }
            pressedAt = Qt.point(mouse.x, mouse.y)
            dragged = false
            const atLine = mapToItem(line, mouse.x, mouse.y)
            view.startTextSelection(line.index, line.textPosition(atLine.x, atLine.y))
        }
        onPositionChanged: mouse => {
            if (!pressed || mouse.buttons !== Qt.LeftButton)
                return
            dragged = dragged || Math.abs(mouse.x - pressedAt.x) + Math.abs(mouse.y - pressedAt.y) > 4
            if (dragged)
                line.ListView.view.extendTextSelection(mapToItem(line.ListView.view, mouse.x, mouse.y))
        }
        onReleased: mouse => {
            const view = line.ListView.view
            view.stopAutoScroll()
            if (dragged || mouse.button !== Qt.LeftButton)
                return
            // A click, not a drag: open the link under it, or drop the selection.
            const point = mapToItem(bodyText, mouse.x, mouse.y)
            const link = bodyText.linkAt(point.x, point.y)
            view.clearTextSelection()
            if (link !== "")
                backend.openLink(link)
        }
        onDoubleClicked: mouse => {
            if (line.kind !== "text")
                return
            const atLine = mapToItem(line, mouse.x, mouse.y)
            bodyText.cursorPosition = line.textPosition(atLine.x, atLine.y)
            bodyText.selectWord()
            line.ListView.view.setTextSelection(line.index, bodyText.selectionStart, line.index, bodyText.selectionEnd)
        }
    }

    // Right-click: answer, react, copy.
    AppMenu {
        id: lineMenu
        readonly property var quick: ["👍", "❤️", "😂", "😮", "😢", "🙏"]

        Item {
            implicitWidth: emojiRow.implicitWidth + 12
            implicitHeight: 40
            Row {
                id: emojiRow
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: lineMenu.quick
                    delegate: ToolButton {
                        required property string modelData
                        width: 34
                        height: 34
                        text: modelData
                        font.pixelSize: 18
                        background: Rectangle {
                            radius: Math.min(4, theme.radius)
                            color: parent.hovered ? line.ui.hover : "transparent"
                        }
                        onClicked: {
                            backend.toggleReaction(line.messageId, modelData)
                            lineMenu.close()
                        }
                    }
                }
            }
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("Reply")
            onTriggered: backend.startReply(line.messageId)
        }
        AppMenuItem {
            text: qsTr("Copy text")
            enabled: line.kind === "text"
            onTriggered: backend.copyText(line.body)
        }
    }
}
