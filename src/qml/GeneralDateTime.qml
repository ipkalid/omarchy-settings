import QtQuick

SettingsPage {
    id: page
    pane: "general"
    lists: ["timezones"]
    settings: ["desktop.barPosition"]
    readonly property bool vertical: ["left", "right"].indexOf(backend.values["desktop.barPosition"]) >= 0

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
        footer: page.vertical ? qsTr("The bar is on the side of the screen, so its clock is stacked.")
                              : qsTr("With the bar on the left or right, the clock takes a stacked style instead.")
        ChoiceRow {
            visible: !page.vertical
            setting: "general.clockFormat"
            label: qsTr("Clock in the bar")
            menuWidth: Math.round(260 * ui.scale)
            options: {
                const _ = translator.language
                return (backend.meta("general.clockFormat").choices || []).map(f => ({ value: f, text: Qt.locale().toString(page.now, f) }))
            }
        }
        ChoiceRow {
            visible: page.vertical
            setting: "general.clockVerticalFormat"
            label: qsTr("Clock in the bar")
            menuWidth: Math.round(260 * ui.scale)
            options: {
                const _ = translator.language
                return (backend.meta("general.clockVerticalFormat").choices || [])
                    .map(f => ({ value: f, text: Qt.locale().toString(page.now, f).split("\n").join(" / ") }))
            }
        }
    }
}
