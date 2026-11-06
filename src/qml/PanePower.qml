import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "power"
    title: win.caps.battery ? qsTr("Battery") : qsTr("Power")
    tagline: page.profileNames[backend.values["power.profile"]] || ""
    glyph: win.caps.battery ? "\u{F0079}" : "\u{F0241}"
    description: qsTr("Choose between saving energy and full performance.")
    lists: ["powerProfiles", "battery"]
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
            options: (backend.lists.powerProfiles || []).map(p => ({ value: p, text: page.profileNames[p] || p }))
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
