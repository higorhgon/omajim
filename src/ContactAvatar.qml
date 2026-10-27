import QtQuick
import QtQuick.Controls

// A contact's picture, or their initial on a color that stays the same for
// them, with the presence dot in the corner. Pictures are true circles, so
// they keep their shape whatever the window rounding.
Item {
    id: avatar

    property string jid
    property string name
    property string source
    property string presence: "offline"
    // A group chat shows # instead of an initial.
    property bool isRoom: false
    property color ringColor: win.panelColor

    implicitWidth: 36
    implicitHeight: 36

    // The same hue for the same address on every run and every machine.
    readonly property color fallbackColor: {
        let hash = 0
        for (let i = 0; i < jid.length; ++i)
            hash = (hash * 31 + jid.charCodeAt(i)) >>> 0
        return Qt.hsla((hash % 360) / 360, 0.42, 0.42, 1)
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: picture.status !== Image.Ready
        color: avatar.fallbackColor

        Label {
            anchors.centerIn: parent
            text: avatar.isRoom ? "#" : avatar.name.trim().charAt(0).toLocaleUpperCase()
            color: "white"
            font.pixelSize: Math.round(avatar.height * 0.42)
            font.weight: Font.DemiBold
        }
    }

    Image {
        id: picture
        anchors.fill: parent
        source: avatar.source
        sourceSize: Qt.size(width * 2, height * 2)
        smooth: true
        mipmap: true
        asynchronous: true
    }

    // The ring in the background color separates the dot from the picture.
    Rectangle {
        width: Math.round(avatar.width * 0.36)
        height: width
        radius: width / 2
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: -1
        anchors.bottomMargin: -1
        color: avatar.ringColor

        PresenceDot {
            anchors.centerIn: parent
            width: parent.width - 4
            height: width
            presence: avatar.presence
            border.color: "#8b8b94"
        }
    }
}
