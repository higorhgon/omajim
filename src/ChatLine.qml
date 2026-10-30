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
    // 0 waiting, 1 sent, 2 delivered, 3 read (HistoryEntry::SendState).
    required property int sendState
    required property string sender
    required property string url
    required property bool edited
    required property bool forwarded
    required property string xhtml
    required property string preview
    required property string route
    required property bool selected
    required property var reply
    required property var reactions
    required property var smileys
    // The picture, when the message is one custom smiley and nothing else.
    readonly property string sticker: {
        backend.smileys.revision
        const texts = Object.keys(smileys || {})
        if (kind !== "text" || texts.length !== 1 || body.trim() !== texts[0])
            return ""
        const path = backend.smileys.pathOf(smileys[texts[0]])
        return path === "" ? "" : "file://" + path
    }

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
        font.weight: Font.DemiBold
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
        // snug bubble and long ones wrap at the maximum width. Until the
        // measure is laid out (Text only does it once the line is complete)
        // the widest: born narrow, a long link would wrap into a line many
        // times its height, and the list, placing it at the end, would keep
        // recreating it as it shrinks, without end.
        width: Math.min(line.width * 0.72, line.kind === "file" ? 420
                        : line.sticker !== "" ? Math.max(stickerImage.Layout.preferredWidth, Math.ceil(stampRow.implicitWidth) + 2,
                                                         reactionRow.implicitWidth) + 28
                        : measure.implicitWidth <= 0 ? line.width * 0.72
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

            // In a group chat, who wrote it, in a color that stays theirs;
            // our own nickname too, as Gajim shows it.
            Label {
                visible: line.sender !== ""
                text: line.sender
                font.pixelSize: 12
                font.weight: Font.DemiBold
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
                        font.weight: Font.DemiBold
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
                TapHandler {
                    onTapped: {
                        const row = backend.messages.rowOfMessage(line.reply.id)
                        if (row >= 0)
                            line.ListView.view.showRow(row)
                    }
                }
            }

            // A message that's a custom smiley alone, large, as ZapFast shows
            // a sticker; a GIF or WebP plays.
            AnimatedImage {
                id: stickerImage
                visible: line.sticker !== ""
                source: line.sticker
                // Up to 160 a side, keeping its shape (from the picture's own
                // size: an Image's implicit one follows what it's given).
                readonly property real fit: sourceSize.width > 0
                                            ? Math.min(160 / sourceSize.width, 160 / sourceSize.height) : 0
                Layout.preferredWidth: Math.round(sourceSize.width * fit)
                Layout.preferredHeight: Math.round(sourceSize.height * fit)
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }

            // Escaped and linkified in C++; a link opens in the browser.
            TextEdit {
                id: bodyText
                Layout.fillWidth: true
                visible: line.kind === "text" && line.sticker === ""
                text: {
                    // Again when a custom smiley's picture arrives.
                    backend.smileys.revision
                    return backend.formatMessage(line.body, line.ui.accent, line.ui.muted, line.smileys, line.forwarded,
                                                 line.xhtml, bubble.color, line.ui.foreground)
                }
                textFormat: TextEdit.RichText
                readOnly: true
                // Selection is the conversation's, so it can run across
                // messages; the pointer below drives it.
                selectByMouse: false
                persistentSelection: true
                wrapMode: TextEdit.Wrap
                color: line.ui.foreground
                // The accent, as text selections go in most apps: the theme's
                // own selection color is a gray for terminals, close to the
                // bubbles' and lost on them.
                selectionColor: line.ui.accent
                selectedTextColor: line.ui.accentText
                font.pixelSize: 14
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
                    TapHandler { onTapped: backend.openFile(line.localPath) }
                }

                Rectangle {
                    id: pictureBar
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
                                id: pictureAction
                                required property var modelData
                                implicitWidth: 34
                                implicitHeight: 34
                                pointerOver: line.pointerOver(pictureAction)
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
                    id: fileBadge
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
                    TapHandler { onTapped: if (line.localPath !== "") backend.openFile(line.localPath) }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 3

                    // The whole name, as Gajim shows it, over two lines at most.
                    Label {
                        id: fileName
                        Layout.fillWidth: true
                        text: line.body
                        color: line.ui.foreground
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        maximumLineCount: 2
                        elide: Text.ElideMiddle
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
                            id: acceptButton
                            primary: true
                            implicitHeight: 32
                            pointerOver: line.pointerOver(acceptButton)
                            text: qsTr("Accept")
                            onClicked: backend.acceptFile(line.messageId)
                        }
                        AppButton {
                            id: declineButton
                            flat: true
                            implicitHeight: 32
                            pointerOver: line.pointerOver(declineButton)
                            text: qsTr("Decline")
                            onClicked: backend.refuseFile(line.messageId)
                        }
                    }
                }

                Repeater {
                    model: line.fileActions
                    delegate: IconButton {
                        id: fileAction
                        required property var modelData
                        Layout.alignment: Qt.AlignVCenter
                        pointerOver: line.pointerOver(fileAction)
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
                        radius: Math.min(4, theme.radius)
                        readonly property bool lit: line.pointerOver(chip)
                        color: modelData.mine ? Qt.alpha(line.ui.accent, lit ? 0.4 : 0.25) : lit ? line.ui.hover : line.ui.background
                        border.color: modelData.mine ? line.ui.accent : line.ui.border
                        Label {
                            id: chipLabel
                            anchors.centerIn: parent
                            text: "%1 %2".arg(chip.modelData.emoji).arg(chip.modelData.count)
                            color: line.ui.foreground
                            font.pixelSize: 12
                        }
                        ToolTip.visible: lit
                        ToolTip.text: chip.modelData.who
                        TapHandler { onTapped: backend.toggleReaction(line.messageId, chip.modelData.emoji) }
                    }
                }
                // Any other emoji, from the picker, as Gajim's reactions bar.
                Rectangle {
                    id: addChip
                    implicitWidth: 34
                    implicitHeight: 24
                    radius: Math.min(4, theme.radius)
                    readonly property bool lit: line.pointerOver(addChip)
                    color: lit ? line.ui.hover : line.ui.background
                    border.color: line.ui.border
                    AppIcon {
                        anchors.centerIn: parent
                        width: 15
                        height: 15
                        name: "smile-plus"
                        color: line.ui.muted
                    }
                    ToolTip.visible: lit
                    ToolTip.text: qsTr("Add a reaction")
                    TapHandler { onTapped: line.ListView.view.pickReaction(addChip, line.messageId) }
                }
            }

            // When it was written, and whether it was forwarded, edited or
            // delivered.
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
                    visible: line.forwarded
                    text: qsTr("(forwarded)")
                    color: line.ui.muted
                    font.pixelSize: 11
                    font.italic: true
                }
                Label {
                    visible: line.edited
                    text: qsTr("(edited)")
                    color: line.ui.muted
                    font.pixelSize: 11
                    font.italic: true
                }
                // How far ours got, as ZapFast shows it: a clock while it
                // waits to go out, one check once the server has it, two
                // once the other side does, two in the accent once read.
                AppIcon {
                    id: deliveredMark
                    visible: line.outgoing
                    implicitWidth: 15
                    implicitHeight: 15
                    name: line.sendState === 0 ? "delivery-pending" : line.sendState === 1 ? "delivery-tick" : "delivery-ticks"
                    // Read in ZapFast's blue, for dark themes or light ones.
                    color: line.sendState === 3 ? (line.ui.background.hslLightness < 0.5 ? "#53bdeb" : "#027eb5")
                                                : line.ui.muted
                    ToolTip.visible: line.pointerOver(deliveredMark)
                    ToolTip.text: line.sendState === 0 ? qsTr("Waiting to be sent")
                                : line.sendState === 1 ? qsTr("Sent to the server")
                                : line.sendState === 2 ? qsTr("Received") : qsTr("Read")
                }
            }
        }
    }

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

    // Whether the pointer is over a part of the bubble. The click area over
    // the bubble keeps hover from reaching what's under it, so its buttons
    // and chips light up from this instead.
    function pointerOver(item) {
        if (!lineHover.hovered || !item.visible)
            return false
        const point = hoverZone.mapToItem(item, lineHover.point.position.x, lineHover.point.position.y)
        return item.contains(point)
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

        // Covering the bubble, this area's cursor is the one that shows, so
        // it picks it for what's under the pointer: the text cursor on a
        // text's bubble, as in ZapFast, wherever a drag would select; a hand
        // on links and on what opens or reacts.
        function cursorAt(x, y) {
            const over = item => item.visible && item.contains(mapToItem(item, x, y))
            if (line.kind === "text") {
                if (over(quote))
                    return Qt.PointingHandCursor
                if (over(reactionRow)) {
                    const point = mapToItem(reactionRow, x, y)
                    return reactionRow.childAt(point.x, point.y) ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
                const point = mapToItem(bodyText, x, y)
                return over(bodyText) && bodyText.linkAt(point.x, point.y) !== "" ? Qt.PointingHandCursor : Qt.IBeamCursor
            }
            if (line.localPath !== "" && (over(pictureCard) && !over(pictureBar) || over(fileBadge) || over(fileName)))
                return Qt.PointingHandCursor
            return Qt.ArrowCursor
        }
        cursorShape: {
            if (pressed && !picking && line.kind === "text")
                return Qt.IBeamCursor
            if (!lineHover.hovered)
                return Qt.ArrowCursor
            const point = hoverZone.mapToItem(pointer, lineHover.point.position.x, lineHover.point.position.y)
            return cursorAt(point.x, point.y)
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
                // A range only from a message already picked; the first
                // Shift+click picks just this one.
                const from = view.selectionAnchor >= 0 && backend.messages.selectedCount > 0
                           ? view.selectionAnchor : line.messageId
                if (!line.selected)
                    backend.messages.selectRange(from, line.messageId)
                view.selectionAnchor = line.messageId
                backend.messages.holdSelection()
                view.startPicking(line.messageId)
                mouse.accepted = true
                return
            }
            // A plain click picks nothing, so it starts no range.
            view.selectionAnchor = -1
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

    // An item of the line's menu keeping a smiley it has as ours.
    component KeepSmiley: AppMenuItem {
        property int slot
        readonly property var smiley: lineMenu.received[slot]
        visible: smiley !== undefined
        height: visible ? implicitHeight : 0
        text: smiley ? qsTr("Add %1 to your smileys").arg(smiley.text) : ""
        onTriggered: line.ListView.view.addToSmileys(smiley.path, smiley.text)
    }

    // Right-click: answer, react, edit, copy, keep a smiley.
    AppMenu {
        id: lineMenu
        readonly property var quick: ["👍", "❤️", "😂", "😮", "😢", "🙏"]
        // Ours, text, and recent enough; asked as it opens, since it expires.
        property bool editable: false
        // Custom smileys in it, with their picture here, not ours yet.
        property var received: []
        onAboutToShow: {
            editable = backend.canEdit(line.messageId)
            const smileys = line.smileys || {}
            received = Object.keys(smileys)
                .map(text => ({text: text, path: backend.smileys.pathOf(smileys[text]), own: backend.smileys.isOwn(smileys[text])}))
                .filter(smiley => smiley.path !== "" && !smiley.own)
        }

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
            text: qsTr("Edit")
            visible: lineMenu.editable
            height: visible ? implicitHeight : 0
            onTriggered: line.ListView.view.editMessage(line.messageId)
        }
        AppMenuItem {
            text: qsTr("Copy text")
            enabled: line.kind === "text"
            onTriggered: backend.copyText(line.body)
        }
        AppMenuSeparator {
            visible: lineMenu.received.length > 0
            height: visible ? implicitHeight : 0
        }
        // One per smiley (up to three), fixed items shown as needed: items a
        // Menu gets afterwards draw but don't take the pointer.
        KeepSmiley { slot: 0 }
        KeepSmiley { slot: 1 }
        KeepSmiley { slot: 2 }
    }
}
