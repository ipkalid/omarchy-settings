import QtQuick

SettingsPage {
    id: page
    pane: "general"
    lists: ["timezones"]

    // A tick so the clock previews stay current.
    property date now: new Date()
    Timer { interval: 30000; running: true; repeat: true; onTriggered: page.now = new Date() }

    SettingsGroup {
        footer: qsTr("Changing the time zone asks for your password.")
        ToggleRow {
            setting: "general.autoTime"
            label: qsTr("Set time automatically")
            sublabel: qsTr("Keeps the clock right using time servers on the internet")
        }
        ChoiceRow {
            setting: "general.timezone"
            label: qsTr("Time zone")
            menuWidth: 260
            options: (backend.lists.timezones || []).map(t => ({ value: t, text: t.replace(/_/g, " ") }))
        }
    }

    SettingsGroup {
        title: qsTr("Clock")
        ChoiceRow {
            setting: "general.clockFormat"
            label: qsTr("Clock in the bar")
            menuWidth: 260
            options: {
                const _ = translator.language
                const formats = ["dddd HH:mm", "HH:mm", "h:mm AP", "dddd h:mm AP", "ddd d MMM HH:mm", "ddd d MMM h:mm AP"]
                return formats.map(f => ({ value: f, text: Qt.locale().toString(page.now, f) }))
            }
        }
    }
}
