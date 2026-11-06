import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "notifications"
    title: qsTr("Notifications")
    tagline: backend.values["notifications.doNotDisturb"] === true ? qsTr("Do Not Disturb") : backend.values["notifications.doNotDisturb"] === false ? qsTr("Showing notifications") : ""
    glyph: "\u{F009A}"
    description: qsTr("Silence notifications when you need to focus.")
    lists: ["crashMutes"]

    SettingsGroup {
        footer: qsTr("Notifications still arrive and wait in the notification center; they just don't pop up.")
        ToggleRow {
            setting: "notifications.doNotDisturb"
            label: qsTr("Do Not Disturb")
            glyph: "\u{F009B}"
        }
    }

    SettingsGroup {
        title: qsTr("Crashes")
        footer: qsTr("When an app crashes, Omarchy tells you and can explain why. Programs you silenced from such a notification are listed here.")
        ToggleRow { setting: "notifications.crashReports"; label: qsTr("Tell me when an app crashes") }
        Repeater {
            model: backend.lists.crashMutes || []
            delegate: SettingRow {
                required property string modelData
                label: modelData
                sublabel: qsTr("Crashes are silent")
                OmaButton {
                    text: qsTr("Notify Again")
                    onClicked: backend.act("crash.unmute", [modelData], ["crashMutes"])
                }
            }
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
