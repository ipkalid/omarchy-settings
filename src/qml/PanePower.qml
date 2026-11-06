import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    resetPane: "power"
    pane: "power"
    title: win.caps.battery ? qsTr("Battery") : qsTr("Power")
    tagline: page.profileNames[backend.values["power.profile"]] || ""
    glyph: win.caps.battery ? "\u{F0079}" : "\u{F0241}"
    description: qsTr("Choose between saving energy and full performance.")
    lists: ["powerProfiles", "battery", "power"]
    settings: ["lock.stayAwake"]
    pollMs: 30000

    readonly property var battery: backend.lists.battery || ({})
    readonly property var profileNames: ({
        "power-saver": qsTr("Low Power"),
        "balanced": qsTr("Balanced"),
        "performance": qsTr("High Performance")
    })

    SettingsGroup {
        visible: battery.percent !== undefined
        title: qsTr("Battery")
        InfoRow {
            label: qsTr("Charge")
            value: battery.percent !== undefined ? qsTr("%1%").arg(Number(battery.percent).toLocaleString(Qt.locale(), 'f', 0)) : ""
        }
        InfoRow {
            label: qsTr("Status")
            value: ({ "Charging": qsTr("Charging"), "Discharging": qsTr("On battery"), "Full": qsTr("Fully charged"),
                      "Not charging": qsTr("Not charging") })[battery.status] || battery.status || ""
        }
    }

    SettingsGroup {
        title: qsTr("Energy")
        footer: qsTr("Low Power slows the processor down to save energy and keep things cool. High Performance does the opposite.")
        ChoiceRow {
            setting: "power.profile"
            label: qsTr("Power mode")
            sublabel: win.caps.battery ? qsTr("Right now") : ""
            options: (backend.lists.powerProfiles || []).map(p => ({ value: p, text: page.profileNames[p] || p }))
        }
        ChoiceRow {
            visible: win.caps.battery === true
            setting: "power.acProfile"
            label: qsTr("On the power adapter")
            options: (backend.lists.powerProfiles || []).map(p => ({ value: p, text: page.profileNames[p] || p }))
        }
        ChoiceRow {
            visible: win.caps.battery === true
            setting: "power.batteryProfile"
            label: qsTr("On battery")
            options: (backend.lists.powerProfiles || []).map(p => ({ value: p, text: page.profileNames[p] || p }))
        }
    }

    SettingsGroup {
        title: qsTr("Hibernation")
        footer: qsTr("Hibernating saves everything to disk and turns the computer off, so it uses no power and resumes where you left off.")
        ButtonRow {
            label: (backend.lists.power || {}).hibernation ? qsTr("Hibernation is set up") : qsTr("Hibernation is not set up")
            sublabel: (backend.lists.power || {}).hibernation ? qsTr("It shows in the system menu") : qsTr("Sets up a swap file and asks for your password")
            text: (backend.lists.power || {}).hibernation ? qsTr("Remove…") : qsTr("Set Up…")
            onClicked: backend.act("launch", [(backend.lists.power || {}).hibernation ? "hibernationRemove" : "hibernationSetup"])
        }
    }

    SettingsGroup {
        footer: qsTr("Keeping the computer awake stops the screensaver and the lock screen until you turn this off.")
        ToggleRow {
            setting: "lock.stayAwake"
            label: qsTr("Keep the computer awake")
        }
        ToggleRow {
            setting: "power.suspendInMenu"
            label: qsTr("Show Suspend in the system menu")
        }
    }
}
