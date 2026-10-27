import QtQuick

// A true circle, so it keeps its round shape whatever the window rounding.
Rectangle {
    property string presence: "offline"

    implicitWidth: 10
    implicitHeight: 10
    radius: width / 2
    color: {
        switch (presence) {
        case "online":
        case "chat": return "#4ade80"
        case "away": return "#facc15"
        case "xa": return "#fb923c"
        case "dnd": return "#f87171"
        case "connecting": return "#8b8b94"
        }
        return "transparent"
    }
    border.width: presence === "offline" ? 1.5 : 0
    border.color: "#6b6b74"
}
