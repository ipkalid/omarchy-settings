import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "notifications"
    title: qsTr("Notifications")
    tagline: backend.values["notifications.doNotDisturb"] === true ? qsTr("Do Not Disturb") : backend.values["notifications.doNotDisturb"] === false ? qsTr("Showing notifications") : ""
    glyph: "\u{F009A}"
    description: qsTr("Silence notifications when you need to focus.")

    SettingsGroup {
        footer: qsTr("Notifications still arrive and wait in the notification center; they just don't pop up.")
        ToggleRow {
            setting: "notifications.doNotDisturb"
            label: qsTr("Do Not Disturb")
            glyph: "\u{F009B}"
        }
    }

    SettingsGroup {
        ButtonRow {
            label: qsTr("See how notifications look")
            text: qsTr("Send a Test")
            onClicked: backend.act("notifications.test", [])
        }
    }
}
