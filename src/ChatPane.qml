import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The open conversation, on the theme's background like Hype's stage: who
// it's with, the messages, and the field to write in.
Rectangle {
    id: pane

    readonly property var ui: theme.colors
    color: ui.background

    // Copies what's selected in the conversation: whole messages picked with
    // Shift+click, or text dragged across them. False when there's nothing.
    function copySelection() {
        if (backend.messages.selectedCount > 0) {
            backend.copySelectedMessages()
            backend.messages.clearSelection()
            return true
        }
        if (messages.hasTextSelection()) {
            messages.copyTextSelection()
            return true
        }
        return false
    }

    // Searching this conversation (Ctrl+F): the matching lines, oldest
    // first, and the one shown.
    property var searchHits: []
    property int searchIndex: -1

    function openSearch() {
        searchBar.visible = true
        searchField.forceActiveFocus()
        searchField.selectAll()
    }
    function closeSearch() {
        searchBar.visible = false
        searchHits = []
        searchIndex = -1
        messages.clearTextSelection()
        composer.forceActiveFocus()
    }
    function runSearch() {
        searchHits = backend.searchConversation(searchField.text)
        // The newest match first, as it's the nearest.
        searchIndex = searchHits.length - 1
        showHit()
    }
    function stepSearch(delta) {
        if (searchHits.length === 0)
            return
        searchIndex = (searchIndex + delta + searchHits.length) % searchHits.length
        showHit()
    }
    function showHit() {
        if (searchIndex < 0) {
            messages.clearTextSelection()
            return
        }
        const row = backend.revealMessage(searchHits[searchIndex])
        if (row < 0)
            return
        messages.showRow(row)
        Qt.callLater(() => {
            const item = messages.itemAtIndex(row)
            const found = item ? item.findText(searchField.text) : null
            if (found)
                messages.setTextSelection(row, found.start, row, found.end)
        })
    }

    // A picture or files in the clipboard: confirm before sending, as Gajim
    // does. Text pastes as text. True when there was something to offer.
    function offerPaste() {
        if (backend.currentChat === "")
            return false
        const content = backend.clipboardContent()
        if (content.kind === "files") {
            pasteDialog.paths = content.paths
            pasteDialog.open()
            return true
        }
        if (content.kind === "image") {
            const path = backend.savePastedImage()
            if (path === "")
                return true
            pasteDialog.paths = [path]
            pasteDialog.open()
            return true
        }
        return false
    }

    function send() {
        if (backend.sendComposed(composerFormat.runs(composer.textDocument))) {
            setComposerText("")
            messages.toEnd()
        }
    }

    // The message field is rich text: what goes in from outside goes in as
    // text, never as markup.
    function setComposerText(text) {
        pendingFormat = null
        composerFormat.setPlainText(composer.textDocument, text)
        composer.cursorPosition = composer.length
    }
    function insertInComposer(text) {
        const start = composer.selectionStart, end = composer.selectionEnd
        if (end > start)
            composer.remove(start, end)
        composerFormat.insertPlain(composer.textDocument, start, text)
        composer.cursorPosition = start + text.length
        composer.forceActiveFocus()
    }
    function pastePlain() {
        const text = backend.clipboardText()
        if (text !== "")
            insertInComposer(text)
        return true
    }

    // Formatting as Pidgin's toolbar does it: on the selected text, or with
    // nothing selected, on what's typed next (the letters after it follow
    // the one before them).
    property var pendingFormat: null
    function format(apply, start, end) {
        composer.forceActiveFocus()
        if (end > start) {
            apply(start, end)
            composer.select(start, end)
        } else {
            pendingFormat = {apply: apply, at: start, length: composer.length}
        }
    }
    readonly property var colors: [
        {name: qsTr("Red"), value: "#e01b24"}, {name: qsTr("Orange"), value: "#e66100"},
        {name: qsTr("Yellow"), value: "#c88800"}, {name: qsTr("Green"), value: "#26a269"},
        {name: qsTr("Teal"), value: "#0f8b8d"}, {name: qsTr("Blue"), value: "#1c71d8"},
        {name: qsTr("Purple"), value: "#9141ac"}, {name: qsTr("Pink"), value: "#d6338a"},
        {name: qsTr("Brown"), value: "#865e3c"}, {name: qsTr("Gray"), value: "#77767b"}]
    // Light, so dark letters read on them, on Pidgin's white too.
    readonly property var backgrounds: [
        {name: qsTr("Yellow"), value: "#fff3a0"}, {name: qsTr("Green"), value: "#c8f0c8"},
        {name: qsTr("Blue"), value: "#cfe3ff"}, {name: qsTr("Pink"), value: "#ffd3e6"},
        {name: qsTr("Orange"), value: "#ffdcb8"}, {name: qsTr("Purple"), value: "#e5d4f5"},
        {name: qsTr("Gray"), value: "#e0e0e0"}]

    // An emoji into the message, where the cursor is.
    function pickEmojiForMessage() {
        emojiPicker.openAt(emojiButton, emoji => pane.insertInComposer(emoji), pane.sendSmiley)
    }

    // One of our smileys goes out at once, a message of its own, as a
    // sticker; while correcting a message it goes in the field instead.
    function sendSmiley(shortcut) {
        if (backend.editing) {
            insertInComposer(shortcut)
        } else if (backend.sendMessage(shortcut)) {
            messages.toEnd()
            composer.forceActiveFocus()
        }
    }

    // Ctrl+C copies what's selected in the conversation. While the message
    // field has focus it claims Ctrl+C even with nothing selected in it, so
    // it hands the key on itself (below); this covers the rest.
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: !composer.activeFocus && (backend.messages.selectedCount > 0 || messages.textStart !== null)
        onActivated: pane.copySelection()
    }
    Shortcut {
        sequence: "Escape"
        enabled: backend.messages.selectedCount > 0 || messages.textStart !== null
        onActivated: {
            backend.messages.clearSelection()
            messages.clearTextSelection()
        }
    }

    Shortcut {
        sequences: [StandardKey.Find]
        context: Qt.ApplicationShortcut
        enabled: backend.currentChat !== ""
        onActivated: pane.openSearch()
    }

    Connections {
        target: backend
        function onCurrentChatChanged() {
            searchBar.visible = false
            pane.searchHits = []
            pane.searchIndex = -1
            if (backend.currentChat !== "")
                composer.forceActiveFocus()
        }
        function onReplyChanged() {
            if (backend.replyingTo !== "")
                composer.forceActiveFocus()
        }
    }

    ColumnLayout {
        anchors.centerIn: parent
        visible: backend.currentChat === ""
        spacing: 8
        AppIcon {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 40
            Layout.preferredHeight: 40
            name: "message"
            color: pane.ui.border
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Pick a conversation")
            color: pane.ui.foreground
            font.pixelSize: 16
            font.weight: Font.DemiBold
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Ctrl+K searches your contacts. ? lists every shortcut.")
            color: pane.ui.muted
        }
    }

    ColumnLayout {
        anchors.fill: parent
        visible: backend.currentChat !== ""
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            Layout.leftMargin: 20
            Layout.rightMargin: 14
            spacing: 12

            ContactAvatar {
                implicitWidth: 32
                implicitHeight: 32
                jid: backend.currentChat
                name: backend.currentChatName
                source: backend.currentChatAvatar
                presence: backend.currentChatPresence
                isRoom: backend.currentChatIsRoom
                ringColor: pane.ui.background
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    Layout.fillWidth: true
                    text: backend.currentChatName
                    color: pane.ui.foreground
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                // A room shows its subject, or whether we're in it; a person
                // their status, or that they're typing.
                Label {
                    Layout.fillWidth: true
                    text: backend.currentChatIsRoom
                          ? (backend.currentChatStatusText !== "" ? backend.currentChatStatusText
                             : backend.currentChatPresence === "online" ? qsTr("Group chat") : qsTr("Not in the room"))
                          : backend.currentChatTyping ? qsTr("typing…")
                          : backend.currentChatStatusText !== ""
                          ? "%1 — %2".arg(win.presenceLabel(backend.currentChatPresence)).arg(backend.currentChatStatusText)
                          : win.presenceLabel(backend.currentChatPresence)
                    color: backend.currentChatTyping && !backend.currentChatIsRoom ? pane.ui.accent : pane.ui.muted
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
            AppButton {
                id: peopleButton
                visible: backend.currentChatIsRoom && backend.currentRoomParticipants.length > 0
                flat: true
                implicitHeight: 32
                text: qsTr("%n people", "", backend.currentRoomParticipants.length)
                onClicked: participantsPopup.open()

                RoomPanel {
                    id: participantsPopup
                    y: parent.height + 4
                    x: parent.width - width
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.preferredHeight: 1
            color: pane.ui.border
        }

        // Ctrl+F: find words in this conversation's whole history.
        RowLayout {
            id: searchBar
            objectName: "conversationSearch"
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 14
            Layout.topMargin: 10
            visible: false
            spacing: 6

            AppTextField {
                id: searchField
                Layout.fillWidth: true
                implicitHeight: 34
                placeholderText: qsTr("Search this conversation")
                onTextChanged: searchDelay.restart()
                Keys.onReturnPressed: event => pane.stepSearch(event.modifiers & Qt.ShiftModifier ? 1 : -1)
                Keys.onEnterPressed: event => pane.stepSearch(event.modifiers & Qt.ShiftModifier ? 1 : -1)
                Keys.onEscapePressed: pane.closeSearch()
                Timer {
                    id: searchDelay
                    interval: 200
                    onTriggered: pane.runSearch()
                }
            }
            Label {
                text: searchField.text.trim() === "" ? ""
                      : pane.searchHits.length === 0 ? qsTr("Nothing found")
                      : qsTr("%1 of %2").arg(pane.searchIndex + 1).arg(pane.searchHits.length)
                color: pane.ui.muted
                font.pixelSize: 12
            }
            IconButton {
                iconName: "chevron-up"
                description: qsTr("Older (Enter)")
                enabled: pane.searchHits.length > 0
                onClicked: pane.stepSearch(-1)
            }
            IconButton {
                iconName: "chevron-down"
                description: qsTr("Newer (Shift+Enter)")
                enabled: pane.searchHits.length > 0
                onClicked: pane.stepSearch(1)
            }
            IconButton {
                iconName: "close"
                description: qsTr("Close the search (Esc)")
                onClicked: pane.closeSearch()
            }
        }

        ListView {
            id: messages
            objectName: "messages"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            topMargin: 16
            bottomMargin: 16
            model: backend.messages
            boundsBehavior: Flickable.StopAtBounds
            // The message a Shift+click range starts from.
            property var selectionAnchor: -1

            // A text selection that can run across messages: where it
            // started and where the pointer is now, as {row, pos}.
            property var textStart: null
            property var textEnd: null
            property point lastPointer
            property real scrollSpeed: 0
            signal textSelectionChanged()

            function orderedTextSelection() {
                const s = textStart, e = textEnd
                const backwards = e.row < s.row || (e.row === s.row && e.pos < s.pos)
                return backwards ? {first: e, last: s} : {first: s, last: e}
            }
            function hasTextSelection() {
                return textStart !== null && (textStart.row !== textEnd.row || textStart.pos !== textEnd.pos)
            }
            function startTextSelection(row, pos) {
                textStart = {row: row, pos: pos}
                textEnd = {row: row, pos: pos}
                textSelectionChanged()
            }
            function setTextSelection(startRow, startPos, endRow, endPos) {
                textStart = {row: startRow, pos: startPos}
                textEnd = {row: endRow, pos: endPos}
                textSelectionChanged()
            }
            function clearTextSelection() {
                if (textStart === null)
                    return
                textStart = null
                textEnd = null
                textSelectionChanged()
            }
            // Shift+drag: the message it began on, picking every message
            // from there to the pointer; -1 when not picking.
            property var pickFrom: -1
            function startPicking(messageId) {
                pickFrom = messageId
            }
            function stopPicking() {
                pickFrom = -1
            }

            // One of our lines back in the message field, to correct it.
            function editMessage(messageId) {
                const text = backend.startEditing(messageId)
                if (text === "")
                    return
                pane.setComposerText(text)
                composer.forceActiveFocus()
            }

            // A reaction from the emoji picker, opened over a line's bubble.
            function pickReaction(item, messageId) {
                emojiPicker.openAt(item, emoji => backend.addReaction(messageId, emoji))
            }

            // A smiley of a message, to keep as ours.
            function addToSmileys(path, shortcut) {
                newSmileyDialog.offer(path, shortcut)
            }

            // The pointer, in this view's coordinates, moved while selecting
            // text or picking messages.
            function extendSelection(point) {
                lastPointer = point
                // Near the top or bottom edge the conversation scrolls along.
                scrollSpeed = point.y < 32 ? -Math.min(24, (32 - point.y) / 2 + 4)
                            : point.y > height - 32 ? Math.min(24, (point.y - height + 32) / 2 + 4) : 0
                const y = Math.max(1, Math.min(height - 2, point.y))
                // Between two bubbles, the one just below counts.
                const item = itemAt(point.x + contentX, y + contentY) || itemAt(point.x + contentX, y + contentY + spacing)
                if (!item || item.textPosition === undefined)
                    return
                if (pickFrom >= 0) {
                    backend.messages.dragRange(pickFrom, item.messageId)
                    selectionAnchor = item.messageId
                    return
                }
                if (textStart === null)
                    return
                const local = mapToItem(item, point.x, y)
                textEnd = {row: item.index, pos: item.textPosition(local.x, local.y)}
                textSelectionChanged()
            }
            function stopAutoScroll() {
                scrollSpeed = 0
            }
            function copyTextSelection() {
                if (!hasTextSelection())
                    return
                const range = orderedTextSelection()
                const parts = []
                for (let row = range.first.row; row <= range.last.row; ++row) {
                    const item = itemAtIndex(row)
                    const start = row === range.first.row ? range.first.pos : 0
                    const end = row === range.last.row ? range.last.pos : -1
                    // Lines scrolled out of view come whole from the history.
                    parts.push({row: row, text: item ? item.textBetween(start, end) : null})
                }
                backend.copyTranscript(parts)
            }

            Timer {
                interval: 30
                repeat: true
                running: messages.scrollSpeed !== 0
                onTriggered: {
                    messages.contentY = Math.max(messages.originY - messages.topMargin,
                                                 Math.min(messages.contentY + messages.scrollSpeed,
                                                          messages.originY + messages.contentHeight - messages.height + messages.bottomMargin))
                    messages.extendSelection(messages.lastPointer)
                    messages.followEnd = messages.distanceToEnd < 24
                }
            }
            // Another conversation starts with nothing selected.
            onModelChanged: clearTextSelection()
            Connections {
                target: backend
                function onCurrentChatChanged() { messages.clearTextSelection() }
            }
            ScrollBar.vertical: ScrollBar {
                id: messagesBar
                onPressedChanged: if (!pressed) messages.followEnd = messages.distanceToEnd < 24
            }
            delegate: ChatLine {}

            // The conversation stays at its end, as lines come and as they
            // settle into their height (a line laid out after it's placed
            // would otherwise leave the end a little below), until the
            // user scrolls up; back at the end, it follows again.
            property bool followEnd: true
            // Lines that came while scrolled up, for the button down.
            property int countWhenLeft: 0
            readonly property real distanceToEnd: originY + contentHeight + bottomMargin - contentY - height
            function toEnd() {
                followEnd = true
                // positionViewAtEnd() leaves out the bottom margin, a bit
                // short of the end; then the rest of the way.
                positionViewAtEnd()
                contentY = Math.max(originY - topMargin, originY + contentHeight + bottomMargin - height)
            }
            function showRow(row) {
                followEnd = false
                positionViewAtIndex(row, ListView.Center)
            }
            onFollowEndChanged: if (!followEnd) countWhenLeft = count
            // The wheel scrolls without "movement ended": up leaves the end,
            // down to it follows again. Only watched; the list scrolls.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: wheel => {
                    wheel.accepted = false
                    if (wheel.angleDelta.y > 0 || wheel.pixelDelta.y > 0)
                        messages.followEnd = false
                    else
                        Qt.callLater(() => messages.followEnd = messages.distanceToEnd < 24)
                }
            }
            onMovementEnded: followEnd = distanceToEnd < 24
            onCountChanged: if (followEnd) Qt.callLater(toEnd)
            onContentHeightChanged: if (followEnd) Qt.callLater(toEnd)
            onHeightChanged: if (followEnd) Qt.callLater(toEnd)
            Connections {
                target: backend
                function onCurrentChatChanged() { Qt.callLater(messages.toEnd) }
                function onCurrentChatReselected() { messages.toEnd() }
            }

            // Back to the end, as ZapFast has it, with how many lines came
            // meanwhile.
            AppButton {
                id: toEndButton
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                // Its right edge with the bubbles' (ChatLine's 20).
                anchors.rightMargin: 20
                anchors.bottomMargin: 12
                width: 40
                height: 40
                padding: 0
                visible: messages.count > 0 && messages.distanceToEnd > messages.height / 3
                Accessible.name: qsTr("Go to the newest message")
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: qsTr("Go to the newest message")
                background: Rectangle {
                    radius: theme.radius
                    color: toEndButton.hovered ? Qt.tint(pane.ui.panel, pane.ui.hover) : pane.ui.panel
                    border.color: pane.ui.border
                }
                contentItem: Item {
                    AppIcon {
                        anchors.centerIn: parent
                        width: 20
                        height: 20
                        name: "chevron-down"
                        color: pane.ui.foreground
                    }
                }
                onClicked: messages.toEnd()

                Rectangle {
                    readonly property int fresh: messages.count - messages.countWhenLeft
                    visible: !messages.followEnd && fresh > 0
                    anchors.horizontalCenter: parent.right
                    anchors.verticalCenter: parent.top
                    anchors.horizontalCenterOffset: -6
                    anchors.verticalCenterOffset: 6
                    width: Math.max(18, freshLabel.implicitWidth + 8)
                    height: 18
                    radius: 9
                    color: theme.colors.accent
                    Label {
                        id: freshLabel
                        anchors.centerIn: parent
                        text: parent.fresh
                        color: theme.colors.accentText
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: messages.count === 0
                text: qsTr("No messages with %1 yet.").arg(backend.currentChatName)
                color: pane.ui.muted
            }
        }

        // The message being answered; Esc or the cross drops it.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 8
            visible: backend.replyingTo !== ""
            implicitHeight: 40
            radius: Math.min(4, theme.radius)
            color: pane.ui.panel
            border.color: pane.ui.border

            Rectangle {
                width: 3
                height: parent.height
                color: pane.ui.accent
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 4
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Replying to %1").arg(backend.replyingTo)
                    color: pane.ui.muted
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                IconButton {
                    iconName: "close"
                    description: qsTr("Don't reply (Esc)")
                    onClicked: backend.cancelReply()
                }
            }
        }

        // The message being corrected; Esc or the cross leaves it as it was.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 8
            visible: backend.editing
            implicitHeight: 40
            radius: Math.min(4, theme.radius)
            color: pane.ui.panel
            border.color: pane.ui.border

            Rectangle {
                width: 3
                height: parent.height
                color: pane.ui.accent
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 4
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Editing: %1").arg(backend.editingText.replace(/\s+/g, " "))
                    color: pane.ui.muted
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                IconButton {
                    iconName: "close"
                    description: qsTr("Don't edit (Esc)")
                    onClicked: {
                        backend.cancelEditing()
                        pane.setComposerText("")
                    }
                }
            }
        }

        // Picked messages, and what to do with them.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 10
            visible: backend.messages.selectedCount > 0
            spacing: 10
            Label {
                Layout.fillWidth: true
                text: qsTr("%n messages selected · Shift+click or drag adds or removes", "", backend.messages.selectedCount)
                color: pane.ui.muted
                font.pixelSize: 13
                elide: Text.ElideRight
            }
            AppButton {
                flat: true
                implicitHeight: 32
                text: qsTr("Clear")
                onClicked: backend.messages.clearSelection()
            }
            AppButton {
                flat: true
                implicitHeight: 32
                text: qsTr("Forward")
                onClicked: forwardDialog.open()
            }
            AppButton {
                primary: true
                font.weight: Font.Normal
                implicitHeight: 32
                text: qsTr("Copy")
                onClicked: {
                    backend.copySelectedMessages()
                    backend.messages.clearSelection()
                }
            }
        }

        // The message field, framed like Hype's editors: the panel color,
        // a border that takes the accent while you write.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.bottomMargin: 20
            implicitHeight: Math.min(180, composerRow.implicitHeight + 16)
            radius: theme.radius
            color: pane.ui.panel
            border.color: composer.activeFocus ? pane.ui.accent : pane.ui.border

            RowLayout {
                id: composerRow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                IconButton {
                    id: attachButton
                    Layout.alignment: Qt.AlignBottom
                    iconName: "paperclip"
                    description: qsTr("Send a file")
                    checked: sendMenu.visible
                    onClicked: sendMenu.toggle(attachButton, 0, -sendMenu.implicitHeight - 4)

                    AppMenu {
                        id: sendMenu
                        AppMenuItem {
                            text: qsTr("Through the server")
                            hint: "Ctrl+O"
                            enabled: backend.serverUploadAvailable
                            onTriggered: backend.chooseFileToSend("server")
                        }
                        // Straight to the person's device, as Pidgin takes
                        // files; a room has no device to send to.
                        AppMenuItem {
                            text: qsTr("Directly")
                            hint: "Ctrl+Shift+O"
                            enabled: !backend.currentChatIsRoom
                            onTriggered: backend.chooseFileToSend("direct")
                        }
                    }
                }

                // Bold, colors and the rest, as Pidgin's toolbar: Pidgin
                // users see them all (XHTML-IM), Gajim's the bold, italic,
                // strike and code (XEP-0393).
                IconButton {
                    id: formatButton
                    Layout.alignment: Qt.AlignBottom
                    iconName: "format"
                    description: qsTr("Format")
                    checked: formatMenu.visible
                    // The menu takes the focus, and the selection with it.
                    property int selectionStart: 0
                    property int selectionEnd: 0
                    property var current: ({})
                    onClicked: {
                        selectionStart = composer.selectionStart
                        selectionEnd = composer.selectionEnd
                        current = composerFormat.formatOf(composer.textDocument, selectionStart, selectionEnd)
                        formatMenu.toggle(formatButton, 0, -formatMenu.implicitHeight - 4)
                    }

                    AppMenu {
                        id: formatMenu
                        hasChecks: true
                        Repeater {
                            model: [{label: qsTr("Bold"), kind: "bold"},
                                    {label: qsTr("Italic"), kind: "italic"},
                                    {label: qsTr("Underline"), kind: "underline"},
                                    {label: qsTr("Strike"), kind: "strike"},
                                    {label: qsTr("Code"), kind: "code"}]
                            AppMenuItem {
                                required property var modelData
                                text: modelData.label
                                checkable: true
                                checked: !!formatButton.current[modelData.kind]
                                onTriggered: pane.format((start, end) => composerFormat.toggle(composer.textDocument, start, end, modelData.kind),
                                                         formatButton.selectionStart, formatButton.selectionEnd)
                            }
                        }
                        AppMenuItem {
                            text: qsTr("Code block")
                            checkable: true
                            checked: !!formatButton.current.block
                            onTriggered: {
                                composer.forceActiveFocus()
                                composerFormat.toggleBlock(composer.textDocument, formatButton.selectionStart, formatButton.selectionEnd)
                            }
                        }
                        AppMenuSeparator {}
                        AppMenu {
                            title: qsTr("Text color")
                            hasChecks: true
                            Repeater {
                                model: pane.colors
                                AppMenuItem {
                                    required property var modelData
                                    text: modelData.name
                                    swatch: modelData.value
                                    onTriggered: pane.format((start, end) => composerFormat.setColor(composer.textDocument, start, end, modelData.value, false),
                                                             formatButton.selectionStart, formatButton.selectionEnd)
                                }
                            }
                            AppMenuSeparator {}
                            AppMenuItem {
                                text: qsTr("No color")
                                onTriggered: pane.format((start, end) => composerFormat.setColor(composer.textDocument, start, end, "", false),
                                                         formatButton.selectionStart, formatButton.selectionEnd)
                            }
                        }
                        AppMenu {
                            title: qsTr("Background color")
                            hasChecks: true
                            Repeater {
                                model: pane.backgrounds
                                AppMenuItem {
                                    required property var modelData
                                    text: modelData.name
                                    swatch: modelData.value
                                    onTriggered: pane.format((start, end) => composerFormat.setColor(composer.textDocument, start, end, modelData.value, true),
                                                             formatButton.selectionStart, formatButton.selectionEnd)
                                }
                            }
                            AppMenuSeparator {}
                            AppMenuItem {
                                text: qsTr("No color")
                                onTriggered: pane.format((start, end) => composerFormat.setColor(composer.textDocument, start, end, "", true),
                                                         formatButton.selectionStart, formatButton.selectionEnd)
                            }
                        }
                        AppMenuSeparator {}
                        AppMenuItem {
                            text: qsTr("Clear formatting")
                            enabled: formatButton.selectionEnd > formatButton.selectionStart
                            onTriggered: {
                                composer.forceActiveFocus()
                                composerFormat.clearFormat(composer.textDocument, formatButton.selectionStart, formatButton.selectionEnd)
                            }
                        }
                    }
                }

                IconButton {
                    id: emojiButton
                    Layout.alignment: Qt.AlignBottom
                    iconName: "smile"
                    description: qsTr("Insert an emoji (Ctrl+.)")
                    checked: emojiPicker.visible && emojiPicker.opener === emojiButton
                    onClicked: pane.pickEmojiForMessage()
                }

                // As tall as its text, centered in the frame, so the
                // placeholder sits where the first line will; it grows with
                // more lines and scrolls past the frame's height.
                ScrollView {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: Math.min(composer.implicitHeight, 160)
                    TextArea {
                        id: composer
                        // Rich, for Pidgin's colors; text goes in only as text.
                        textFormat: TextEdit.RichText
                        // The format menu keeps what's selected.
                        persistentSelection: true
                        wrapMode: TextEdit.Wrap
                        background: null
                        color: pane.ui.foreground
                        selectionColor: pane.ui.selection
                        selectedTextColor: pane.ui.selectionText
                        font.pixelSize: 14
                        ContextMenu.menu: TextContextMenu {
                            target: composer
                            pasteHandler: () => pane.offerPaste() || pane.pastePlain()
                        }
                        onTextChanged: {
                            backend.setComposing(length > 0)
                            // What was typed after picking a format with
                            // nothing selected takes it.
                            const pending = pane.pendingFormat
                            if (pending && length > pending.length) {
                                pane.pendingFormat = null
                                const typed = length - pending.length
                                if (cursorPosition === pending.at + typed)
                                    pending.apply(pending.at, pending.at + typed)
                            }
                        }
                        onCursorPositionChanged: {
                            if (pane.pendingFormat && length === pane.pendingFormat.length
                                    && cursorPosition !== pane.pendingFormat.at)
                                pane.pendingFormat = null
                        }
                        // Ctrl+C with nothing selected here copies what's
                        // selected in the conversation; Ctrl+V with a picture
                        // or files in the clipboard offers to send them;
                        // Ctrl+. or Ctrl+; opens the emoji picker, as in GTK's
                        // fields.
                        Keys.onPressed: event => {
                            if ((event.modifiers & Qt.ControlModifier)
                                    && (event.key === Qt.Key_Period || event.key === Qt.Key_Semicolon)) {
                                pane.pickEmojiForMessage()
                                event.accepted = true
                            } else if (event.matches(StandardKey.Copy) && composer.selectedText === "" && pane.copySelection()) {
                                event.accepted = true
                            } else if (event.matches(StandardKey.Paste)) {
                                // Pictures and files are offered; text goes
                                // in as text, never as a web page's styling.
                                if (!pane.offerPaste())
                                    pane.pastePlain()
                                event.accepted = true
                            }
                        }
                        // Up in an empty field brings back our last line to correct.
                        Keys.onUpPressed: event => {
                            const last = length === 0 ? backend.startEditingLast() : ""
                            if (last !== "") {
                                pane.setComposerText(last)
                            } else {
                                event.accepted = false
                            }
                        }
                        Keys.onEscapePressed: event => {
                            if (backend.editing) {
                                backend.cancelEditing()
                                pane.setComposerText("")
                            } else if (backend.replyingTo !== "") {
                                backend.cancelReply()
                            } else {
                                event.accepted = false
                            }
                        }
                        // Enter sends; Shift+Enter starts a new line.
                        Keys.onReturnPressed: event => {
                            if (event.modifiers & Qt.ShiftModifier)
                                event.accepted = false
                            else
                                pane.send()
                        }
                        Keys.onEnterPressed: event => Keys.returnPressed(event)

                        Label {
                            x: composer.leftPadding
                            y: composer.topPadding
                            visible: composer.length === 0 && !composer.preeditText
                            text: backend.editing ? qsTr("Correcting your message (Esc cancels)")
                                  : backend.connectionState === "online" ? qsTr("Message %1").arg(backend.currentChatName)
                                  : qsTr("You're offline")
                            color: backend.editing ? pane.ui.accent : pane.ui.muted
                            font: composer.font
                        }
                    }
                }

                // An icon, quieter than a word for something sent many times a minute.
                IconButton {
                    Layout.alignment: Qt.AlignBottom
                    implicitWidth: 38
                    implicitHeight: 38
                    primary: true
                    iconName: backend.editing ? "check" : "send"
                    description: backend.editing ? qsTr("Correct") : qsTr("Send")
                    enabled: composer.getText(0, composer.length).trim() !== "" && backend.connectionState === "online"
                    onClicked: pane.send()
                }
            }
        }
    }

    EmojiPicker {
        id: emojiPicker
        newSmiley: newSmileyDialog
    }

    NewSmileyDialog {
        id: newSmileyDialog
    }

    // Where to forward the picked messages, as ZapFast asks: the recent
    // conversations, or any contact or room by name.
    AppDialog {
        id: forwardDialog
        property var destinations: []

        title: qsTr("Forward %n messages", "", backend.messages.selectedCount)
        subtitle: qsTr("Each goes with who wrote it and when, and the conversation opens.")
        acceptText: qsTr("Forward")
        acceptEnabled: destinationList.currentIndex >= 0
        onAboutToShow: {
            forwardSearch.text = ""
            destinations = backend.contacts.destinations("")
            destinationList.currentIndex = destinations.length > 0 ? 0 : -1
            forwardSearch.forceActiveFocus()
        }
        onClosed: composer.forceActiveFocus()
        onAccepted: backend.forwardSelected(destinations[destinationList.currentIndex].jid)

        AppTextField {
            id: forwardSearch
            Layout.fillWidth: true
            placeholderText: qsTr("Search contacts")
            onTextChanged: {
                forwardDialog.destinations = backend.contacts.destinations(text)
                destinationList.currentIndex = forwardDialog.destinations.length > 0 ? 0 : -1
            }
            Keys.onReturnPressed: forwardDialog.accept()
            Keys.onEnterPressed: forwardDialog.accept()
            Keys.onDownPressed: destinationList.incrementCurrentIndex()
            Keys.onUpPressed: destinationList.decrementCurrentIndex()
        }
        ListView {
            id: destinationList
            Layout.fillWidth: true
            Layout.preferredHeight: 280
            clip: true
            model: forwardDialog.destinations
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: destination
                required property var modelData
                required property int index
                width: ListView.view.width
                height: 44
                radius: Math.min(4, theme.radius)
                color: ListView.isCurrentItem ? pane.ui.hover : "transparent"
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 10
                    ContactAvatar {
                        implicitWidth: 28
                        implicitHeight: 28
                        jid: destination.modelData.jid
                        name: destination.modelData.name
                        source: destination.modelData.avatar
                        presence: destination.modelData.presence
                        isRoom: destination.modelData.isRoom
                        ringColor: destination.ListView.isCurrentItem ? pane.ui.hover : pane.ui.panel
                    }
                    Label {
                        Layout.fillWidth: true
                        text: destination.modelData.name
                        color: pane.ui.foreground
                        elide: Text.ElideRight
                    }
                }
                TapHandler {
                    onTapped: destinationList.currentIndex = destination.index
                    onDoubleTapped: {
                        destinationList.currentIndex = destination.index
                        forwardDialog.accept()
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                visible: forwardDialog.destinations.length === 0
                text: qsTr("No contact matches “%1”.").arg(forwardSearch.text)
                color: pane.ui.muted
            }
        }
    }

    AppDialog {
        id: pasteDialog
        property var paths: []
        readonly property bool single: paths.length === 1
        readonly property bool picture: single && /\.(png|jpe?g|gif|webp|bmp)$/i.test(paths[0])

        title: single ? qsTr("Send to %1?").arg(backend.currentChatName)
                      : qsTr("Send %n files to %1?", "", paths.length).arg(backend.currentChatName)
        subtitle: paths.map(path => path.split("/").pop()).join(", ")
        acceptText: qsTr("Send")
        onClosed: composer.forceActiveFocus()
        onAccepted: {
            const method = backend.serverUploadAvailable || backend.currentChatIsRoom ? "server" : "direct"
            for (const path of paths)
                backend.sendFile(path, method)
        }

        Image {
            Layout.fillWidth: true
            // A fixed box the picture fits into; its own size isn't known
            // until the layout gives it a width.
            Layout.preferredHeight: 220
            visible: pasteDialog.picture
            source: pasteDialog.picture ? "file://" + pasteDialog.paths[0] : ""
            sourceSize.width: 800
            fillMode: Image.PreserveAspectFit
            // Loaded at once, so the dialog doesn't grow after it opens.
            asynchronous: false
        }
    }

    // Dropping a file onto the conversation sends it.
    DropArea {
        anchors.fill: parent
        enabled: backend.currentChat !== ""
        onDropped: drop => {
            if (drop.hasUrls) {
                backend.sendFile(drop.urls[0], backend.serverUploadAvailable || backend.currentChatIsRoom ? "server" : "direct")
                drop.accept()
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 12
            visible: parent.containsDrag
            radius: theme.radius
            color: Qt.alpha(pane.ui.accent, 0.08)
            border.color: pane.ui.accent
            border.width: 2
            Label {
                anchors.centerIn: parent
                text: qsTr("Drop to send to %1").arg(backend.currentChatName)
                color: pane.ui.foreground
                font.pixelSize: 16
            }
        }
    }
}
