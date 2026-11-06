import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "lock"
    title: qsTr("Lock Screen")
    tagline: backend.values["lock.stayAwake"] === true ? qsTr("Staying awake") : backend.values["lock.lockAfter"] !== undefined ? qsTr("Locks after %1").arg(page.idleText(backend.values["lock.lockAfter"])) : ""
    glyph: "\u{F033E}"
    description: qsTr("Decide when the screensaver starts and the screen locks while you are away.")

    readonly property var idleOptions: {
        const _ = translator.language
        return [0, 60, 120, 150, 300, 600, 900, 1200, 1800, 3600].map(s => ({ value: s, text: page.idleText(s) }))
    }
    function idleText(seconds) {
        if (seconds === 0) return qsTr("Never")
        if (seconds >= 3600) return qsTr("%n hours", "", seconds / 3600)
        if (seconds % 60 !== 0)
            return qsTr("%1 minutes").arg((seconds / 60).toLocaleString(Qt.locale(), 'f', 1))
        return qsTr("%n minutes", "", seconds / 60)
    }

    SettingsGroup {
        footer: qsTr("Both count from the moment you stop using the keyboard and mouse.")
        ChoiceRow {
            setting: "lock.screensaverAfter"
            label: qsTr("Start the screensaver when idle for")
            options: page.idleOptions
        }
        ChoiceRow {
            setting: "lock.lockAfter"
            label: qsTr("Lock the screen when idle for")
            options: page.idleOptions
        }
        ToggleRow {
            setting: "lock.screensaver"
            label: qsTr("Screensaver")
            sublabel: qsTr("When off, the screen goes straight to the lock screen")
        }
    }

    SettingsGroup {
        ToggleRow {
            setting: "lock.stayAwake"
            label: qsTr("Keep the computer awake")
            sublabel: qsTr("Pauses the screensaver and the lock screen until you turn this off")
        }
        ButtonRow {
            label: qsTr("Lock the screen now")
            text: qsTr("Lock")
            onClicked: backend.act("system.lock", [])
        }
    }
}
