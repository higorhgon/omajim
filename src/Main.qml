import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1100
    height: 720
    minimumWidth: 720
    minimumHeight: 480
    visible: true
    title: backend.contacts.totalUnread > 0 ? "(%1) Omajim".arg(backend.contacts.totalUnread) : "Omajim"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color panelColor: "#151518"
    readonly property color surfaceColor: "#1d1d21"
    readonly property color lineColor: "#2a2a30"
    readonly property color textColor: "#e8e8ea"
    readonly property color mutedColor: "#8b8b94"
    property bool helpVisible: false

    function presenceLabel(presence) {
        switch (presence) {
        case "online": return qsTr("Available")
        case "chat": return qsTr("Free to chat")
        case "away": return qsTr("Away")
        case "xa": return qsTr("Not available")
        case "dnd": return qsTr("Busy")
        case "connecting": return qsTr("Connecting…")
        }
        return qsTr("Offline")
    }

    function moveChat(step) {
        const contacts = backend.contacts
        if (contacts.count === 0)
            return
        const row = contacts.rowOf(backend.currentChat)
        const next = row < 0 ? (step > 0 ? 0 : contacts.count - 1)
                             : Math.max(0, Math.min(contacts.count - 1, row + step))
        backend.openChat(contacts.jidAt(next))
    }

    onActiveChanged: backend.windowActive = active

    Connections {
        target: backend
        function onRaiseRequested() {
            win.raise()
            win.requestActivate()
        }
    }

    // Single keys like ? or Q would land in the message field, so every
    // shortcut here carries a modifier or is a function key.
    Shortcut { sequences: ["F1", "Ctrl+/"]; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: win.helpVisible
        onActivated: win.helpVisible = false
    }
    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.ApplicationShortcut
        enabled: backend.signedIn
        onActivated: contactPane.focusSearch()
    }
    Shortcut {
        sequence: "Ctrl+N"
        context: Qt.ApplicationShortcut
        enabled: backend.signedIn
        onActivated: addContactDialog.open()
    }
    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        enabled: backend.signedIn && backend.currentChat !== ""
        onActivated: backend.chooseFileToSend()
    }
    Shortcut {
        sequence: "Ctrl+W"
        context: Qt.ApplicationShortcut
        enabled: backend.currentChat !== ""
        onActivated: backend.closeChat()
    }
    Shortcut { sequences: ["Alt+Down", "Ctrl+Tab"]; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: win.moveChat(1) }
    Shortcut { sequences: ["Alt+Up", "Ctrl+Shift+Tab"]; context: Qt.ApplicationShortcut; enabled: backend.signedIn; onActivated: win.moveChat(-1) }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                visible: backend.errorText !== ""
                implicitHeight: bannerRow.implicitHeight + 16
                color: "#3a1d1f"

                RowLayout {
                    id: bannerRow
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 8
                    spacing: 8

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: backend.errorText
                            color: win.textColor
                            wrapMode: Text.Wrap
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: backend.certificateFingerprint !== ""
                            text: qsTr("SHA-256 fingerprint: %1").arg(backend.certificateFingerprint)
                            color: win.mutedColor
                            font.family: "monospace"
                            font.pixelSize: 11
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    Button {
                        Material.roundedScale: theme.radius
                        visible: backend.certificateFingerprint !== ""
                        text: qsTr("Trust this certificate")
                        flat: true
                        onClicked: backend.trustCertificate()
                    }
                    Button {
                        Material.roundedScale: theme.radius
                        visible: backend.certificateFingerprint !== ""
                        text: qsTr("Stay offline")
                        flat: true
                        onClicked: backend.rejectCertificate()
                    }
                    ToolButton {
                        Material.roundedScale: theme.radius
                        visible: backend.certificateFingerprint === ""
                        text: "✕"
                        onClicked: backend.dismissError()
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Dismiss")
                    }
                }
            }

            BusyIndicator {
                Layout.alignment: Qt.AlignCenter
                Layout.fillHeight: true
                visible: backend.starting
                running: visible
            }

            SignInView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !backend.starting && !backend.signedIn
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: backend.signedIn
                spacing: 0

                ContactPane {
                    id: contactPane
                    Layout.preferredWidth: 300
                    Layout.fillHeight: true
                    onAddContactRequested: addContactDialog.open()
                }
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: win.lineColor
                }
                ChatPane {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }
        }

        Dialog {
            id: addContactDialog
            title: qsTr("Add contact")
            anchors.centerIn: parent
            modal: true
            width: Math.min(420, parent.width - 32)
            standardButtons: Dialog.Ok | Dialog.Cancel
            Material.roundedScale: theme.radius

            onOpened: {
                newJid.text = ""
                newName.text = ""
                newJid.forceActiveFocus()
            }
            onAccepted: backend.addContact(newJid.text, newName.text)

            ColumnLayout {
                anchors.fill: parent
                spacing: 12
                TextField {
                    Material.roundedScale: Math.min(4, theme.radius)
                    id: newJid
                    Layout.fillWidth: true
                    placeholderText: qsTr("Address (name@server)")
                    inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                    onAccepted: addContactDialog.accept()
                }
                TextField {
                    Material.roundedScale: Math.min(4, theme.radius)
                    id: newName
                    Layout.fillWidth: true
                    placeholderText: qsTr("Name (optional)")
                    onAccepted: addContactDialog.accept()
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("They'll be asked to share their status with you.")
                    color: win.mutedColor
                    wrapMode: Text.Wrap
                }
            }
        }

        ShortcutsOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onCloseRequested: win.helpVisible = false
        }
    }
}
