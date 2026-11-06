import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    resetPane: "lock"
    pane: "lock"
    title: qsTr("Lock Screen")
    tagline: backend.values["lock.stayAwake"] === true ? qsTr("Staying awake") : backend.values["lock.lockAfter"] !== undefined ? qsTr("Locks after %1").arg(page.idleText(backend.values["lock.lockAfter"])) : ""
    glyph: "\u{F033E}"
    description: qsTr("Decide when the screensaver starts and the screen locks while you are away.")
    lists: ["branding"]

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

    BrandingGroup {
        which: "screensaver"
        title: qsTr("Screensaver Art")
        footer: qsTr("The screensaver draws this text. Make it from a picture, or write your own.")
    }

    SettingsGroup {
        NavRow {
            label: qsTr("Boot and Unlock Screen")
            sublabel: qsTr("Where you type the disk password when the computer starts")
            glyph: "\u{F0341}"
            onOpen: win.openSub("LockBootScreen", qsTr("Boot and Unlock Screen"))
        }
    }
}
