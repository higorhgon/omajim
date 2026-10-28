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
        "file-code": "M6 3H14L19 8V21H6Z M14 3V8H19 M10.5 12L8.5 14.5L10.5 17 M14 12L16 14.5L14 17",
        "file-text": "M6 3H14L19 8V21H6Z M14 3V8H19 M9 12.5H16 M9 15.5H16 M9 18.5H13.5",
        "file-image": "M6 3H14L19 8V21H6Z M14 3V8H19 M8.5 18.5L11 15L13.5 17.5L15 16L16.5 18.5 M10.5 11.5A0.8 0.8 0 1 0 10.5 11.6",
        "file-video": "M6 3H14L19 8V21H6Z M14 3V8H19 M10.5 12V17.5L15 14.75Z",
        "file-audio": "M6 3H14L19 8V21H6Z M14 3V8H19 M10.5 18A1.5 1.5 0 1 1 10.5 17.9 M12 18V12L15.5 11",
        "file-archive": "M6 3H14L19 8V21H6Z M14 3V8H19 M11 3V5 M13 5V7 M11 7V9 M13 9V11 M11 11V13 M11 15H13V18H11Z",
        "folder": "M3 7A1.5 1.5 0 0 1 4.5 5.5H9.5L11.5 7.5H19.5A1.5 1.5 0 0 1 21 9V17.5A1.5 1.5 0 0 1 19.5 19H4.5A1.5 1.5 0 0 1 3 17.5Z",
        "save": "M5 4H16L20 8V19A1 1 0 0 1 19 20H5A1 1 0 0 1 4 19V5A1 1 0 0 1 5 4Z M8 4V9H15V4 M7.5 20V14H16.5V20",
        "download": "M12 3V15 M7 10L12 15L17 10 M4 15V21H20V15",
        "close": "M6 6L18 18 M18 6L6 18",
        "keyboard": "M3 6H21V18H3Z M7 10H7.5 M11 10H11.5 M15 10H15.5 M7 14H17",
        "logout": "M14 4H19V20H14 M10 8L6 12L10 16 M6 12H16",
        "message": "M21 4H3V17H8V21L13 17H21Z",
        "hash": "M9.5 3.5L7.5 20.5 M16.5 3.5L14.5 20.5 M4.5 9H20.5 M3.5 15H19.5",
        // The emoji picker: its button, adding a reaction, and its sections.
        "sidebar": "M6 4.5H18A2 2 0 0 1 20 6.5V17.5A2 2 0 0 1 18 19.5H6A2 2 0 0 1 4 17.5V6.5A2 2 0 0 1 6 4.5Z M9.5 4.5V19.5",
        "reply": "M9.5 14.5L4.5 9.5L9.5 4.5 M4.5 9.5H14A6 6 0 0 1 20 15.5V19.5",
        "smile": "M12 3A9 9 0 1 0 12 21A9 9 0 1 0 12 3 M8 14C9 15.7 10.4 16.5 12 16.5C13.6 16.5 15 15.7 16 14 M9 9.5V10 M15 9.5V10",
        "smile-plus": "M19.5 12A8.5 8.5 0 1 1 11 3.5 M7.5 14C8.4 15.6 9.6 16.3 11 16.3C12.4 16.3 13.6 15.6 14.5 14 M8.5 9.5V10 M13.5 9.5V10 M19 2.5V8.5 M16 5.5H22",
        "clock": "M12 3A9 9 0 1 0 12 21A9 9 0 1 0 12 3 M12 7V12L15.5 14",
        "leaf": "M5 19C5 10 10 5 20 4C19 14 14 19 5 19Z M5 19L13 11",
        "cup": "M5 9H16V14A5 5 0 0 1 11 19H10A5 5 0 0 1 5 14Z M16 10.5H17.5A2.2 2.2 0 0 1 17.5 14.9H16 M8.5 3.5V6 M12.5 3.5V6",
        "pin": "M12 21C12 21 5 14.5 5 9.5A7 7 0 0 1 19 9.5C19 14.5 12 21 12 21Z M12 7A2.5 2.5 0 1 0 12 12A2.5 2.5 0 1 0 12 7",
        "ball": "M12 3A9 9 0 1 0 12 21A9 9 0 1 0 12 3 M5.6 5.6C9 9 9 15 5.6 18.4 M18.4 5.6C15 9 15 15 18.4 18.4",
        "bulb": "M9.5 18H14.5 M10.5 21H13.5 M12 3A6 6 0 0 0 8.5 13.9V15.5H15.5V13.9A6 6 0 0 0 12 3Z",
        "heart": "M12 20L4.6 12.6A4.6 4.6 0 0 1 12 6.6A4.6 4.6 0 0 1 19.4 12.6Z",
        "flag": "M5.5 21V4 M5.5 4.5H18L15.5 8.5L18 12.5H5.5",
        "format": "M5 6.5V4.5H14V6.5 M9.5 4.5V19.5 M7.5 19.5H11.5 M13.5 12V10.5H20V12 M16.75 10.5V19.5 M15.25 19.5H18.25",
        "sticker": "M6 4H18A2 2 0 0 1 20 6V14L14 20H6A2 2 0 0 1 4 18V6A2 2 0 0 1 6 4Z M14 20V16A2 2 0 0 1 16 14H20"
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
