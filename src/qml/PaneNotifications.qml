import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "notifications"
    title: qsTr("Notifications")
    tagline: backend.values["notifications.doNotDisturb"] === true ? qsTr("Do Not Disturb") : backend.values["notifications.doNotDisturb"] === false ? qsTr("Showing notifications") : ""
    glyph: "\u{F009A}"
    description: qsTr("Silence notifications when you need to focus.")
    lists: ["crashMutes", "reminders"]
    pollMs: 30000

    SettingsGroup {
        footer: qsTr("Notifications still arrive and wait in the notification center; they just don't pop up.")
        ToggleRow {
            setting: "notifications.doNotDisturb"
            label: qsTr("Do Not Disturb")
            glyph: "\u{F009B}"
        }
    }

    SettingsGroup {
        title: qsTr("Reminders")
        footer: qsTr("A reminder is a notification at a time you pick, like a kitchen timer.")
        Repeater {
            model: (backend.lists.reminders || {}).reminders || []
            delegate: SettingRow {
                required property var modelData
                label: modelData.message || qsTr("Reminder")
                sublabel: qsTr("At %1, in %2").arg(modelData.atTime).arg(modelData.remaining)
                glyph: "\u{F0954}"
            }
        }
        SettingRow {
            label: qsTr("Remind me in")
            ChoiceBox {
                id: inMinutes
                maxWidth: Math.round(160 * ui.scale)
                model: [5, 10, 15, 30, 45, 60, 120, 240].map(m => ({ value: m, text: m < 60 ? qsTr("%n minutes", "", m) : qsTr("%n hours", "", m / 60) }))
                textRole: "text"
                valueRole: "value"
                currentIndex: 2
                Accessible.name: qsTr("When")
            }
            OmaField {
                id: reminderText
                width: Math.round(200 * ui.scale)
                placeholderText: qsTr("What about?")
                onAccepted: setReminder.clicked()
            }
            OmaButton {
                id: setReminder
                text: qsTr("Set")
                onClicked: { backend.act("reminder.set", [inMinutes.currentValue, reminderText.text], ["reminders"]); reminderText.text = "" }
            }
        }
        ButtonRow {
            visible: ((backend.lists.reminders || {}).count || 0) > 0
            label: ""
            text: qsTr("Clear All")
            onClicked: backend.act("reminder.clear", [], ["reminders"])
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
