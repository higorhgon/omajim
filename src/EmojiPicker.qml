import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The emoji picker the Python Omajim had from GTK, beside the message field
// and on reactions: the ones used last, the rest by section, or found by
// name in English or Portuguese. A right-click on a hand or a person picks
// the skin tone.
Popup {
    id: picker

    readonly property var ui: theme.colors
    // What to do with the emoji picked, and what it was opened from; set by
    // whoever opens it.
    property var onPick: null
    property Item opener: null
    // A press on the button that opened it closes it first; the click that
    // follows mustn't open it again.
    property double closedAt: 0
    // -1 shows the recent ones.
    property int section: 0
    readonly property bool searching: searchField.text.trim() !== ""
    readonly property var shown: searching ? emojis.search(searchField.text)
                               : section < 0 ? emojis.recent : emojis.section(section)
    readonly property bool portuguese: translator.language.startsWith("pt")
    property string hoveredName: ""

    readonly property var sections: [
        {icon: "clock", title: qsTr("Recently used")},
        {icon: "smile", title: qsTr("Smileys & People")},
        {icon: "leaf", title: qsTr("Animals & Nature")},
        {icon: "cup", title: qsTr("Food & Drink")},
        {icon: "pin", title: qsTr("Travel & Places")},
        {icon: "ball", title: qsTr("Activities")},
        {icon: "bulb", title: qsTr("Objects")},
        {icon: "heart", title: qsTr("Symbols")},
        {icon: "flag", title: qsTr("Flags")}
    ]

    // Opens over (or under, if there's no room) an item, calling pick with
    // the emoji chosen.
    function openAt(item, pick) {
        if (opener === item && Date.now() - closedAt < 300)
            return
        onPick = pick
        opener = item
        const point = item.mapToItem(parent, 0, 0)
        x = Math.min(point.x, parent.width - width - 8)
        y = point.y - height - 6 >= 8 ? point.y - height - 6 : point.y + item.height + 6
        open()
    }

    function choose(emoji) {
        emojis.use(emoji)
        const pick = onPick
        close()
        opener = null
        if (pick)
            pick(emoji)
    }

    function nameOf(item) {
        return portuguese && item.localName !== "" ? item.localName : item.name
    }

    parent: Overlay.overlay
    width: 352
    height: 400
    padding: 8
    margins: 8
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle {
        radius: theme.radius
        color: picker.ui.panel
        border.color: picker.ui.border
    }

    onAboutToShow: {
        searchField.text = ""
        section = emojis.recent.length > 0 ? -1 : 0
        grid.currentIndex = -1
        searchField.forceActiveFocus()
    }
    onClosed: {
        tonePopup.close()
        closedAt = Date.now()
    }

    contentItem: ColumnLayout {
        spacing: 6

        AppTextField {
            id: searchField
            Layout.fillWidth: true
            implicitHeight: 36
            placeholderText: qsTr("Search emoji")
            onTextChanged: grid.currentIndex = picker.searching && picker.shown.length > 0 ? 0 : -1
            // Enter takes the first match; the arrows go on into the grid.
            Keys.onReturnPressed: {
                if (picker.shown.length > 0)
                    picker.choose(picker.shown[Math.max(0, grid.currentIndex)].emoji)
            }
            Keys.onEnterPressed: event => Keys.returnPressed(event)
            Keys.onDownPressed: {
                grid.currentIndex = Math.max(0, grid.currentIndex)
                grid.forceActiveFocus()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: picker.sections
                delegate: IconButton {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: 32
                    iconName: modelData.icon
                    description: modelData.title
                    checked: !picker.searching && picker.section === index - 1
                    onClicked: {
                        searchField.text = ""
                        picker.section = index - 1
                        grid.positionViewAtBeginning()
                    }
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: picker.searching ? (picker.shown.length === 0 ? qsTr("No emoji found") : "")
                  : picker.section < 0 && picker.shown.length === 0 ? qsTr("Emoji you use show up here")
                  : picker.sections[picker.section + 1].title
            visible: text !== ""
            color: picker.ui.muted
            font.pixelSize: 12
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: picker.shown
            cellWidth: Math.floor(width / 8)
            cellHeight: 40
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: true
            ScrollBar.vertical: ScrollBar {}
            Keys.onReturnPressed: {
                if (currentIndex >= 0)
                    picker.choose(picker.shown[currentIndex].emoji)
            }
            Keys.onEnterPressed: event => Keys.returnPressed(event)
            // Up from the first row goes back to the search.
            Keys.onUpPressed: event => {
                if (currentIndex < 8)
                    searchField.forceActiveFocus()
                else
                    event.accepted = false
            }
            Keys.onPressed: event => {
                // Typing goes on in the search.
                if (event.text !== "" && event.text.trim() !== "" && !(event.modifiers & Qt.ControlModifier)) {
                    searchField.forceActiveFocus()
                    searchField.insert(searchField.cursorPosition, event.text)
                    event.accepted = true
                }
            }

            delegate: Rectangle {
                id: cell
                required property var modelData
                required property int index
                width: grid.cellWidth
                height: grid.cellHeight
                radius: Math.min(4, theme.radius)
                // The one Enter takes: picked with the keys, or the first match.
                color: cellMouse.containsMouse || ((grid.activeFocus || picker.searching) && grid.currentIndex === index)
                       ? picker.ui.hover : "transparent"

                Text {
                    anchors.centerIn: parent
                    text: cell.modelData.emoji
                    font.pixelSize: 24
                }
                // A corner mark on those that take a skin tone, as GTK shows.
                Rectangle {
                    visible: cell.modelData.toned
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 3
                    width: 4
                    height: 4
                    radius: 2
                    color: picker.ui.muted
                }
                MouseArea {
                    id: cellMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.PointingHandCursor
                    onContainsMouseChanged: {
                        const name = picker.nameOf(cell.modelData)
                        if (containsMouse)
                            picker.hoveredName = name
                        else if (picker.hoveredName === name)
                            picker.hoveredName = ""
                    }
                    onClicked: mouse => {
                        if (mouse.button === Qt.RightButton && cell.modelData.toned)
                            tonePopup.openFor(cell, cell.modelData.emoji)
                        else if (mouse.button === Qt.LeftButton)
                            picker.choose(cell.modelData.emoji)
                    }
                    onPressAndHold: {
                        if (cell.modelData.toned)
                            tonePopup.openFor(cell, cell.modelData.emoji)
                    }
                }
            }
        }

        // The name of the emoji under the pointer, or of the one Enter takes.
        Label {
            Layout.fillWidth: true
            text: (grid.activeFocus || picker.searching) && grid.currentIndex >= 0 && grid.currentIndex < picker.shown.length
                  ? picker.nameOf(picker.shown[grid.currentIndex]) : picker.hoveredName
            color: picker.ui.foreground
            font.pixelSize: 12
            elide: Text.ElideRight
        }
    }

    // The five skin tones of one emoji, over it.
    Popup {
        id: tonePopup
        property var tones: []

        function openFor(item, emoji) {
            tones = emojis.tones(emoji)
            const point = item.mapToItem(picker.contentItem, 0, 0)
            x = Math.max(0, Math.min(point.x + item.width / 2 - width / 2, picker.availableWidth - width))
            y = point.y - height - 2
            open()
        }

        padding: 4
        background: Rectangle {
            radius: theme.radius
            color: picker.ui.panel
            border.color: picker.ui.accent
        }
        contentItem: Row {
            spacing: 2
            Repeater {
                model: tonePopup.tones
                delegate: ToolButton {
                    required property string modelData
                    width: 38
                    height: 38
                    text: modelData
                    font.pixelSize: 24
                    background: Rectangle {
                        radius: Math.min(4, theme.radius)
                        color: parent.hovered ? picker.ui.hover : "transparent"
                    }
                    onClicked: picker.choose(modelData)
                }
            }
        }
    }
}
