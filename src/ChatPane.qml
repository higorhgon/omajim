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
        messages.positionViewAtIndex(row, ListView.Center)
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
        if (backend.sendMessage(composer.text))
            composer.clear()
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
            font.bold: true
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
                    font.bold: true
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

                Popup {
                    id: participantsPopup
                    y: parent.height + 4
                    x: parent.width - width
                    width: 240
                    height: Math.min(360, participantList.contentHeight + 12)
                    padding: 6
                    background: Rectangle {
                        radius: theme.radius
                        color: pane.ui.panel
                        border.color: pane.ui.border
                    }
                    ListView {
                        id: participantList
                        anchors.fill: parent
                        clip: true
                        model: backend.currentRoomParticipants
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Label {
                            required property string modelData
                            width: ListView.view.width
                            height: 30
                            leftPadding: 10
                            verticalAlignment: Text.AlignVCenter
                            text: modelData
                            color: pane.ui.foreground
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
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
            // The pointer, in this view's coordinates, moved while selecting.
            function extendTextSelection(point) {
                lastPointer = point
                // Near the top or bottom edge the conversation scrolls along.
                scrollSpeed = point.y < 32 ? -Math.min(24, (32 - point.y) / 2 + 4)
                            : point.y > height - 32 ? Math.min(24, (point.y - height + 32) / 2 + 4) : 0
                const y = Math.max(1, Math.min(height - 2, point.y))
                // Between two bubbles, the one just below counts.
                const item = itemAt(point.x + contentX, y + contentY) || itemAt(point.x + contentX, y + contentY + spacing)
                if (!item || item.textPosition === undefined || textStart === null)
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
                    messages.extendTextSelection(messages.lastPointer)
                }
            }
            // Another conversation starts with nothing selected.
            onModelChanged: clearTextSelection()
            Connections {
                target: backend
                function onCurrentChatChanged() { messages.clearTextSelection() }
            }
            ScrollBar.vertical: ScrollBar {}
            delegate: ChatLine {}

            onCountChanged: Qt.callLater(positionViewAtEnd)
            Connections {
                target: backend
                function onCurrentChatChanged() { Qt.callLater(messages.positionViewAtEnd) }
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
                text: qsTr("%n messages selected · Shift+click adds or removes", "", backend.messages.selectedCount)
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
                primary: true
                font.bold: false
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
                    onClicked: sendMenu.visible ? sendMenu.close() : sendMenu.popup(attachButton, 0, -sendMenu.implicitHeight - 4)

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

                // As tall as its text, centered in the frame, so the
                // placeholder sits where the first line will; it grows with
                // more lines and scrolls past the frame's height.
                ScrollView {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredHeight: Math.min(composer.implicitHeight, 160)
                    TextArea {
                        id: composer
                        wrapMode: TextEdit.Wrap
                        background: null
                        color: pane.ui.foreground
                        selectionColor: pane.ui.selection
                        selectedTextColor: pane.ui.selectionText
                        font.pixelSize: 14
                        ContextMenu.menu: TextContextMenu {
                            target: composer
                            pasteHandler: pane.offerPaste
                        }
                        onTextChanged: backend.setComposing(text.length > 0)
                        // Ctrl+C with nothing selected here copies what's
                        // selected in the conversation; Ctrl+V with a picture
                        // or files in the clipboard offers to send them.
                        Keys.onPressed: event => {
                            if (event.matches(StandardKey.Copy) && composer.selectedText === "" && pane.copySelection())
                                event.accepted = true
                            else if (event.matches(StandardKey.Paste) && pane.offerPaste())
                                event.accepted = true
                        }
                        // Up in an empty field brings back our last line to correct.
                        Keys.onUpPressed: event => {
                            const last = text === "" ? backend.startEditingLast() : ""
                            if (last !== "") {
                                text = last
                                cursorPosition = length
                            } else {
                                event.accepted = false
                            }
                        }
                        Keys.onEscapePressed: event => {
                            if (backend.editing) {
                                backend.cancelEditing()
                                clear()
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
                            visible: composer.text === "" && !composer.preeditText
                            text: backend.editing ? qsTr("Correcting your last message (Esc cancels)")
                                  : backend.connectionState === "online" ? qsTr("Message %1").arg(backend.currentChatName)
                                  : qsTr("You're offline")
                            color: backend.editing ? pane.ui.accent : pane.ui.muted
                            font: composer.font
                        }
                    }
                }

                AppButton {
                    Layout.alignment: Qt.AlignBottom
                    primary: true
                    // Sent many times a minute, so it stays as quiet as the field around it.
                    font.bold: false
                    text: backend.editing ? qsTr("Correct") : qsTr("Send")
                    enabled: composer.text.trim() !== "" && backend.connectionState === "online"
                    onClicked: pane.send()
                }
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
