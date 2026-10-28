import QtQuick
import QtQuick.Controls

// A menu in Hype's style: the panel color, the window's corners, and rows
// wide enough for their label and shortcut hint.
Menu {
    id: menu

    property bool hasChecks: false
    readonly property var ui: theme.colors
    // A press on the button that opened it closes it first; the click that
    // follows mustn't open it again.
    property double closedAt: 0

    // Opens it at (x, y) over the button, or closes it if open.
    function toggle(button, x, y) {
        if (visible)
            close()
        else if (Date.now() - closedAt > 300)
            popup(button, x, y)
    }

    onClosed: closedAt = Date.now()

    padding: 6
    margins: 8
    implicitWidth: {
        let widest = 180
        for (let i = 0; i < count; ++i)
            widest = Math.max(widest, itemAt(i).implicitWidth)
        return widest + leftPadding + rightPadding
    }
    background: Rectangle {
        color: menu.ui.panel
        border.color: menu.ui.border
        radius: theme.radius
    }
    delegate: AppMenuItem {}
}
