import QtQuick

// A true circle, so it keeps its round shape whatever the window rounding,
// in the theme's own green, yellow, orange, and red.
Rectangle {
    property string presence: "offline"
    readonly property var ui: theme.colors

    implicitWidth: 10
    implicitHeight: 10
    radius: width / 2
    color: {
        switch (presence) {
        case "online":
        case "chat": return ui.green
        case "away": return ui.yellow
        case "xa": return ui.orange
        case "dnd": return ui.error
        case "connecting": return ui.muted
        }
        return "transparent"
    }
    border.width: presence === "offline" ? 1.5 : 0
    border.color: ui.muted
}
