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

    // A copy of the file on this computer: the file itself once it's here,
    // or the preview of a picture still on the server.
    readonly property string localPath: transferState === "done" && filePath !== "" ? filePath
                                      : preview !== "" ? decodeURIComponent(preview.replace("file://", "")) : ""

    // Size, and how it's going while it isn't done.
    readonly property string fileDetails: [fileSize,
        transferState === "done" ? "" : stateLabel,
        busy || transferState === "offered" ? routeLabel : ""].filter(part => part !== "").join(" · ")

    readonly property string fileIcon: {
        const suffix = body.split(".").pop().toLowerCase()
        if (["png", "jpg", "jpeg", "gif", "webp", "bmp", "svg"].indexOf(suffix) >= 0) return "file-image"
        if (["mp4", "mkv", "webm", "mov", "avi"].indexOf(suffix) >= 0) return "file-video"
        if (["mp3", "ogg", "opus", "flac", "wav", "m4a"].indexOf(suffix) >= 0) return "file-audio"
        if (["zip", "tar", "gz", "xz", "bz2", "7z", "rar", "zst"].indexOf(suffix) >= 0) return "file-archive"
        if (["html", "htm", "css", "js", "ts", "json", "xml", "py", "php", "c", "cpp", "h", "rs", "sh", "sql", "qml"].indexOf(suffix) >= 0) return "file-code"
        if (["txt", "md", "pdf", "doc", "docx", "odt", "rtf", "csv", "xls", "xlsx", "ods"].indexOf(suffix) >= 0) return "file-text"
        return "file"
    }

    // The icon buttons a file's line offers, as {icon, label, action}.
    readonly property var fileActions: {
        const actions = []
        if (kind !== "file")
            return actions
        if (!outgoing && url !== "" && ["remote", "failed", "canceled"].indexOf(transferState) >= 0)
            actions.push({icon: "download", label: transferState === "remote" ? qsTr("Download") : qsTr("Try again"), action: "download"})
        if (busy || (outgoing && transferState === "offered"))
            actions.push({icon: "close", label: qsTr("Cancel"), action: "cancel"})
        if (transferState === "done" && filePath !== "")
            actions.push({icon: "folder", label: qsTr("Open the folder"), action: "folder"})
        if (localPath !== "" && !busy)
            actions.push({icon: "save", label: qsTr("Save as…"), action: "save"})
        return actions
    }

    function runFileAction(action) {
        switch (action) {
        case "download": backend.downloadFile(line.messageId); break
        case "cancel": backend.cancelTransfer(line.messageId); break
        case "folder": backend.openFolder(line.filePath); break
        case "save": backend.saveFileAs(line.localPath, line.body); break
        }
    }
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
        text: line.sender
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
        width: Math.min(line.width * 0.72, line.kind === "file" ? 420
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

        // The whole bubble of a text selects its text, as in ZapFast: the
        // text cursor shows wherever a drag would start a selection. Links,
        // quotes, and reactions keep their own pointer.
        HoverHandler {
            cursorShape: line.kind === "text" ? Qt.IBeamCursor : Qt.ArrowCursor
        }

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 10
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 4

            // In a group chat, who wrote it, in a color that stays theirs;
            // our own nickname too, as Gajim shows it.
            Label {
                visible: line.sender !== ""
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
                id: quote
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

            // A picture as a file: the picture fills the bubble, with the
            // name, size, and actions over its lower edge, as in Gajim.
            Item {
                id: pictureCard
                visible: line.kind === "file" && line.preview !== "" && picture.status === Image.Ready
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(260, picture.implicitWidth > 0
                                        ? width * picture.implicitHeight / picture.implicitWidth : 0)

                Image {
                    id: picture
                    anchors.fill: parent
                    source: line.preview
                    sourceSize.width: 700
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: backend.openFile(line.localPath) }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 46
                    color: Qt.rgba(0, 0, 0, 0.6)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 4
                        spacing: 4
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                Layout.fillWidth: true
                                text: line.body
                                color: "white"
                                font.pixelSize: 12
                                elide: Text.ElideMiddle
                            }
                            Label {
                                Layout.fillWidth: true
                                text: line.fileDetails
                                color: Qt.rgba(1, 1, 1, 0.75)
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        Repeater {
                            model: line.fileActions
                            delegate: IconButton {
                                required property var modelData
                                implicitWidth: 34
                                implicitHeight: 34
                                iconName: modelData.icon
                                description: modelData.label
                                ink: "white"
                                framed: true
                                frameColor: Qt.rgba(1, 1, 1, 0.14)
                                onClicked: line.runFileAction(modelData.action)
                            }
                        }
                    }
                    // Progress along the very bottom while it moves.
                    Rectangle {
                        anchors.bottom: parent.bottom
                        visible: line.busy
                        width: parent.width * line.progress
                        height: 3
                        color: line.ui.accent
                    }
                }
            }

            // Any other file: its kind, name, and size, with the actions on
            // the right, as in Gajim.
            RowLayout {
                visible: line.kind === "file" && !pictureCard.visible
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    implicitWidth: 48
                    implicitHeight: 48
                    radius: Math.min(4, theme.radius)
                    color: line.ui.background
                    border.color: line.ui.border
                    AppIcon {
                        anchors.centerIn: parent
                        width: 26
                        height: 26
                        name: line.fileIcon
                        color: line.ui.accent
                    }
                    HoverHandler { cursorShape: line.localPath !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    TapHandler { onTapped: if (line.localPath !== "") backend.openFile(line.localPath) }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 3

                    // The whole name, as Gajim shows it, over two lines at most.
                    Label {
                        Layout.fillWidth: true
                        text: line.body
                        color: line.ui.foreground
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideMiddle
                        HoverHandler { cursorShape: line.localPath !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        TapHandler { onTapped: if (line.localPath !== "") backend.openFile(line.localPath) }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: line.fileDetails
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
                    // A direct transfer waits for an answer.
                    RowLayout {
                        Layout.topMargin: 4
                        visible: !line.outgoing && line.transferState === "offered"
                        spacing: 8
                        AppButton {
                            primary: true
                            implicitHeight: 32
                            text: qsTr("Accept")
                            onClicked: backend.acceptFile(line.messageId)
                        }
                        AppButton {
                            flat: true
                            implicitHeight: 32
                            text: qsTr("Decline")
                            onClicked: backend.refuseFile(line.messageId)
                        }
                    }
                }

                Repeater {
                    model: line.fileActions
                    delegate: IconButton {
                        required property var modelData
                        Layout.alignment: Qt.AlignVCenter
                        iconName: modelData.icon
                        description: modelData.label
                        framed: true
                        frameColor: line.ui.background
                        onClicked: line.runFileAction(modelData.action)
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
                // Any other emoji, from the picker, as Gajim's reactions bar.
                Rectangle {
                    id: addChip
                    implicitWidth: 34
                    implicitHeight: 24
                    radius: 12
                    color: addHover.hovered ? line.ui.hover : line.ui.background
                    border.color: line.ui.border
                    AppIcon {
                        anchors.centerIn: parent
                        width: 15
                        height: 15
                        name: "smile-plus"
                        color: line.ui.muted
                    }
                    HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
                    ToolTip.visible: addHover.hovered
                    ToolTip.text: qsTr("Add a reaction")
                    TapHandler { onTapped: line.ListView.view.pickReaction(addChip, line.messageId) }
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
    // Answer and react, beside the bubble while the pointer is over it or
    // them, as in ZapFast: on the side facing the middle, centered on the
    // bubble, the reaction next to it.
    Item {
        id: hoverZone
        readonly property real gap: 6
        x: line.outgoing ? bubble.x - quickActions.width - gap : bubble.x
        width: bubble.width + quickActions.width + gap
        height: bubble.height
        HoverHandler { id: lineHover }

        Row {
            id: quickActions
            x: line.outgoing ? 0 : bubble.width + hoverZone.gap
            anchors.verticalCenter: parent.verticalCenter
            layoutDirection: line.outgoing ? Qt.RightToLeft : Qt.LeftToRight
            spacing: 2
            // Not while dragging a selection across it.
            readonly property bool shown: lineHover.hovered && !pointer.pressed
            opacity: shown ? 1 : 0
            enabled: shown
            Behavior on opacity { NumberAnimation { duration: 90 } }

            IconButton {
                id: reactButton
                implicitWidth: 30
                implicitHeight: 30
                iconName: "smile"
                description: qsTr("React")
                onClicked: line.ListView.view.pickReaction(reactButton, line.messageId)
            }
            IconButton {
                implicitWidth: 30
                implicitHeight: 30
                iconName: "reply"
                description: qsTr("Reply")
                onClicked: backend.startReply(line.messageId)
            }
        }
    }

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

    // Where a search term is in the text, ignoring case and accents, as
    // {start, end}; null when it isn't (or the line isn't text).
    function findText(term) {
        if (line.kind !== "text" || term.trim() === "")
            return null
        // One character in, one out, so positions stay those of the text.
        const fold = text => Array.from(text, c => c.normalize("NFD").charAt(0).toLowerCase()).join("")
        const at = fold(bodyText.getText(0, bodyText.length)).indexOf(fold(term.trim()))
        return at < 0 ? null : {start: at, end: at + term.trim().length}
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
        // Shift held at the press: this drag picks whole messages, not text.
        property bool picking: false
        property bool wasSelected: false

        // Over a part of the bubble with a click of its own.
        function onControl(x, y) {
            if (line.kind !== "text")
                return true
            const over = item => item.visible && item.contains(mapToItem(item, x, y))
            return over(quote) || over(reactionRow)
        }

        onPressed: mouse => {
            if (mouse.button === Qt.RightButton) {
                lineMenu.popup()
                mouse.accepted = true
                return
            }
            const view = line.ListView.view
            pressedAt = Qt.point(mouse.x, mouse.y)
            dragged = false
            // Shift+click picks whole messages: from the last one clicked to
            // this, or, on one already picked, just leaves it out (on
            // release, as a drag from it keeps it). Dragging on picks every
            // message the pointer passes, as in ZapFast.
            picking = (mouse.modifiers & Qt.ShiftModifier) !== 0
            if (picking) {
                view.clearTextSelection()
                wasSelected = line.selected
                if (!line.selected)
                    backend.messages.selectRange(view.selectionAnchor >= 0 ? view.selectionAnchor : line.messageId,
                                                 line.messageId)
                view.selectionAnchor = line.messageId
                backend.messages.holdSelection()
                view.startPicking(line.messageId)
                mouse.accepted = true
                return
            }
            view.selectionAnchor = line.messageId
            backend.messages.clearSelection()
            if (onControl(mouse.x, mouse.y)) {
                // Buttons, files, quotes: theirs to handle.
                view.clearTextSelection()
                mouse.accepted = false
                return
            }
            // Anywhere else on a text's bubble: the start of a selection that
            // may run on into other messages, as in ZapFast. Beside or around
            // the text it starts at the nearest character.
            const atLine = mapToItem(line, mouse.x, mouse.y)
            view.startTextSelection(line.index, line.textPosition(atLine.x, atLine.y))
        }
        onPositionChanged: mouse => {
            if (!pressed || mouse.buttons !== Qt.LeftButton)
                return
            dragged = dragged || Math.abs(mouse.x - pressedAt.x) + Math.abs(mouse.y - pressedAt.y) > 4
            if (dragged)
                line.ListView.view.extendSelection(mapToItem(line.ListView.view, mouse.x, mouse.y))
        }
        onReleased: mouse => {
            const view = line.ListView.view
            view.stopAutoScroll()
            view.stopPicking()
            if (picking) {
                if (!dragged && wasSelected)
                    backend.messages.toggleSelected(line.messageId)
                return
            }
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
                // The rest, from the emoji picker.
                IconButton {
                    width: 34
                    height: 34
                    iconName: "smile-plus"
                    description: qsTr("More reactions")
                    onClicked: {
                        lineMenu.close()
                        line.ListView.view.pickReaction(bubble, line.messageId)
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
