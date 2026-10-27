import QtQuick
import QtQuick.Shapes

// A line icon drawn on a 24 px grid, in any color and size, the way Hype
// draws its toolbar icons.
Item {
    id: icon

    required property string name
    property color color: theme.colors.foreground

    implicitWidth: 24
    implicitHeight: 24

    readonly property var paths: ({
        "plus": "M12 5V19 M5 12H19",
        "send": "M4 12L20 4L14 20L11.5 13.5Z M11.5 13.5L20 4",
        "paperclip": "M20 11.5L12 19.5A5 5 0 0 1 5 12.5L13 4.5A3.3 3.3 0 0 1 17.7 9.2L10 16.9A1.7 1.7 0 0 1 7.6 14.5L14.5 7.6",
        "search": "M10.5 4A6.5 6.5 0 1 0 10.5 17A6.5 6.5 0 1 0 10.5 4 M15.5 15.5L20 20",
        "user-add": "M9 4A3.8 3.8 0 1 0 9 11.6A3.8 3.8 0 1 0 9 4 M2.5 20C2.5 16.5 5.2 14.5 9 14.5C12.8 14.5 15.5 16.5 15.5 20 M19 8V14 M16 11H22",
        "users": "M9 4.5A3.5 3.5 0 1 0 9 11.5A3.5 3.5 0 1 0 9 4.5 M2.5 20C2.5 16.5 5.2 14.5 9 14.5C12.8 14.5 15.5 16.5 15.5 20 M15.5 4.8A3.3 3.3 0 0 1 15.5 11.2 M18 14.8C20.2 15.5 21.5 17.3 21.5 20",
        "more": "M12 5.2A0.8 0.8 0 1 0 12 6.8A0.8 0.8 0 1 0 12 5.2 M12 11.2A0.8 0.8 0 1 0 12 12.8A0.8 0.8 0 1 0 12 11.2 M12 17.2A0.8 0.8 0 1 0 12 18.8A0.8 0.8 0 1 0 12 17.2",
        "check": "M5 12.5L10 17.5L19 7",
        "chevron-down": "M6 9L12 15L18 9",
        "chevron-right": "M9 6L15 12L9 18",
        "chevron-up": "M6 15L12 9L18 15",
        "file": "M6 3H14L19 8V21H6Z M14 3V8H19",
        "download": "M12 3V15 M7 10L12 15L17 10 M4 15V21H20V15",
        "close": "M6 6L18 18 M18 6L6 18",
        "keyboard": "M3 6H21V18H3Z M7 10H7.5 M11 10H11.5 M15 10H15.5 M7 14H17",
        "logout": "M14 4H19V20H14 M10 8L6 12L10 16 M6 12H16",
        "message": "M21 4H3V17H8V21L13 17H21Z",
        "hash": "M9.5 3.5L7.5 20.5 M16.5 3.5L14.5 20.5 M4.5 9H20.5 M3.5 15H19.5"
    })

    Shape {
        width: 24
        height: 24
        anchors.centerIn: parent
        scale: Math.min(icon.width, icon.height) / 24
        antialiasing: true
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: "transparent"
            strokeColor: icon.color
            strokeWidth: icon.name === "more" ? 2.6 : 1.8
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.paths[icon.name] || "" }
        }
    }
}
