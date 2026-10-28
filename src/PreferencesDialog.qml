import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// What used to need omajim.conf, by section; Save keeps it, Cancel drops
// what changed here. Synchronizing the history is done at once.
AppDialog {
    id: dialog

    title: qsTr("Preferences")
    width: Math.min(540, parent ? parent.width - 48 : 540)
    acceptText: qsTr("Save")
    acceptEnabled: awayRow.field.acceptableInput && xaRow.field.acceptableInput

    onAboutToShow: {
        awayRow.field.text = backend.awayMinutes
        xaRow.field.text = backend.xaMinutes
        notificationsBox.checked = backend.notifications
        textSmileysBox.checked = backend.textSmileys
        closeChoice.value = backend.closeAction
        syncChoice.value = [30, 90, 365, -1].find(days => !backend.historyReaches(days)) ?? -1
    }
    onAccepted: {
        backend.awayMinutes = parseInt(awayRow.field.text)
        backend.xaMinutes = parseInt(xaRow.field.text)
        backend.notifications = notificationsBox.checked
        backend.textSmileys = textSmileysBox.checked
        backend.closeAction = closeChoice.value
    }

    component Section: Label {
        Layout.topMargin: 10
        font.pixelSize: 11
        font.letterSpacing: 1
        color: dialog.ui.accent
    }

    // A number of minutes after a label, as one line.
    component MinutesRow: RowLayout {
        property alias label: rowLabel.text
        property alias field: minutes
        Layout.fillWidth: true
        spacing: 10
        Label {
            id: rowLabel
            Layout.fillWidth: true
            color: dialog.ui.foreground
            font.pixelSize: 13
        }
        AppTextField {
            id: minutes
            implicitWidth: 64
            implicitHeight: 34
            horizontalAlignment: TextInput.AlignHCenter
            validator: IntValidator { bottom: 0; top: 999 }
            onAccepted: dialog.accept()
        }
        Label {
            text: qsTr("minutes")
            color: dialog.ui.muted
            font.pixelSize: 13
        }
    }

    Section { text: qsTr("Automatic away").toUpperCase() }
    MinutesRow {
        id: awayRow
        label: qsTr("Away after")
    }
    MinutesRow {
        id: xaRow
        label: qsTr("Not available after")
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Minutes without using the computer; 0 turns a step off.")
        color: dialog.ui.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    Section { text: qsTr("Closing the window").toUpperCase() }
    AppSegmented {
        id: closeChoice
        Layout.fillWidth: true
        options: [{label: qsTr("To the tray"), value: "tray", enabled: tray.available},
                  {label: qsTr("Minimize"), value: "minimize"},
                  {label: qsTr("Quit"), value: "quit"}]
    }
    Label {
        Layout.fillWidth: true
        text: closeChoice.value === "tray" ? qsTr("Omajim stays connected; the tray icon brings it back.")
            : closeChoice.value === "minimize" ? qsTr("Stays connected, minimized; only shows on a floating layout with a dock.")
            : qsTr("Disconnects and closes Omajim.")
        color: dialog.ui.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    Section { text: qsTr("Notifications").toUpperCase() }
    AppCheckBox {
        id: notificationsBox
        Layout.leftMargin: -6
        Layout.fillWidth: true
        text: qsTr("Notify me of new messages (never while Busy)")
    }

    Section { text: qsTr("History on the server").toUpperCase() }
    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        AppSegmented {
            id: syncChoice
            Layout.fillWidth: true
            // Gajim's choices; those the history already reaches are off.
            options: [30, 90, 365, -1].map(days => ({
                label: days === 30 ? qsTr("1 month") : days === 90 ? qsTr("3 months")
                     : days === 365 ? qsTr("1 year") : qsTr("Everything"),
                value: days,
                enabled: !backend.historySyncing && !backend.historyReaches(days)
            }))
        }
        AppButton {
            text: qsTr("Synchronize")
            enabled: backend.connectionState === "online" && !backend.historySyncing
                     && !backend.historyReaches(syncChoice.value)
            onClicked: backend.syncHistory(syncChoice.value)
        }
    }
    Label {
        Layout.fillWidth: true
        text: backend.historySyncing ? qsTr("Synchronizing… %n messages so far.", "", backend.historySyncCount)
            : backend.historyReaches(-1) ? qsTr("All of the server's history is here.")
            : backend.connectionState !== "online" ? qsTr("Only while connected.")
            : qsTr("Brings older messages from the server's archive. The first sign-in brings the last week.")
        color: dialog.ui.muted
        font.pixelSize: 12
        wrapMode: Text.Wrap
    }

    Section { text: qsTr("Messages").toUpperCase() }
    AppCheckBox {
        id: textSmileysBox
        Layout.leftMargin: -6
        Layout.fillWidth: true
        text: qsTr("Show text smileys like :) and :beer: as emoji")
    }

}
