import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    resetPane: "keyboard"
    pane: "keyboard"
    title: qsTr("Keyboard")
    tagline: page.layouts.join(" · ")
    glyph: "\u{F030C}"
    description: qsTr("Set how keys repeat, which layouts you type with, and what the special keys do.")
    lists: ["keyboardLayouts"]

    readonly property var allLayouts: backend.lists.keyboardLayouts || []
    readonly property var layouts: (backend.values["keyboard.layouts"] || "").split(",").filter(l => l.length > 0)
    function layoutName(code) {
        const l = allLayouts.find(x => x.code === code)
        return l ? l.name : code
    }
    function setLayouts(list) { backend.set("keyboard.layouts", list.join(",")) }

    SettingsGroup {
        SliderRow {
            setting: "keyboard.repeatRate"
            label: qsTr("Key repeat rate")
            from: 10; to: 80
            fromText: qsTr("Slow"); toText: qsTr("Fast")
        }
        SliderRow {
            setting: "keyboard.repeatDelay"
            label: qsTr("Delay until repeat")
            from: 1000; to: 150; stepSize: 10
            fromText: qsTr("Long"); toText: qsTr("Short")
        }
        ToggleRow { setting: "keyboard.numlock"; label: qsTr("Turn Num Lock on at login") }
        SliderRow {
            visible: backend.values["keyboard.backlight"] !== undefined && backend.values["keyboard.backlight"] !== null
            setting: "keyboard.backlight"
            label: qsTr("Keyboard brightness")
            to: 100
            live: true
            format: v => qsTr("%1%").arg(Math.round(v).toLocaleString(Qt.locale(), 'f', 0))
        }
    }

    SettingsGroup {
        title: qsTr("Input Sources")
        footer: qsTr("The first layout is the one you start with.")
        Repeater {
            model: page.layouts
            delegate: SettingRow {
                required property string modelData
                required property int index
                label: page.layoutName(modelData)
                sublabel: modelData
                OmaButton {
                    glyph: "\u{F0143}"
                    bordered: false
                    enabled: index > 0
                    onClicked: {
                        const l = page.layouts.slice()
                        l.splice(index - 1, 0, l.splice(index, 1)[0])
                        page.setLayouts(l)
                    }
                    tooltip: qsTr("Move up")
                }
                OmaButton {
                    glyph: "\u{F0156}"
                    bordered: false
                    enabled: page.layouts.length > 1
                    onClicked: page.setLayouts(page.layouts.filter((_, i) => i !== index))
                    tooltip: qsTr("Remove")
                }
            }
        }
        SettingRow {
            label: qsTr("Add a layout")
            ChoiceBox {
                id: addLayout
                maxWidth: 240
                currentIndex: -1
                placeholder: qsTr("Choose a layout")
                // A ComboBox selects its first item whenever its model
                // changes; nothing is chosen until the user picks.
                onCountChanged: Qt.callLater(() => currentIndex = -1)
                model: page.allLayouts.filter(l => page.layouts.indexOf(l.code) < 0)
                textRole: "name"
                valueRole: "code"
                Accessible.name: qsTr("Layout to add")
            }
            OmaButton {
                text: qsTr("Add")
                enabled: addLayout.currentIndex >= 0
                onClicked: page.setLayouts(page.layouts.concat([addLayout.currentValue]))
            }
        }
        ChoiceRow {
            setting: "keyboard.switchLayout"
            label: qsTr("Switch layouts with")
            menuWidth: 240
            options: [
                { value: "", text: qsTr("No shortcut") },
                { value: "grp:alt_shift_toggle", text: "Alt + Shift" },
                { value: "grp:ctrl_shift_toggle", text: "Ctrl + Shift" },
                { value: "grp:alts_toggle", text: qsTr("Both Alt keys") },
                { value: "grp:caps_toggle", text: "Caps Lock" },
                { value: "grp:win_space_toggle", text: "Super + Space" }
            ]
        }
    }

    SettingsGroup {
        title: qsTr("Special Keys")
        ButtonRow {
            label: qsTr("Compose sequences")
            sublabel: qsTr("Your own Compose shortcuts, in ~/.XCompose")
            text: qsTr("Edit…")
            onClicked: backend.act("xcompose.edit", [])
        }
        footer: qsTr("The Compose key types characters like é and ß from short sequences: Compose, then e and '.")
        ChoiceRow {
            setting: "keyboard.capsLock"
            label: qsTr("Caps Lock key")
            menuWidth: 240
            options: [
                { value: "", text: qsTr("Caps Lock") },
                { value: "caps:escape", text: qsTr("Escape") },
                { value: "ctrl:nocaps", text: qsTr("Control") },
                { value: "caps:swapescape", text: qsTr("Swap with Escape") },
                { value: "caps:backspace", text: qsTr("Backspace") },
                { value: "caps:none", text: qsTr("Does nothing") }
            ]
        }
        ChoiceRow {
            setting: "keyboard.compose"
            label: qsTr("Compose key")
            menuWidth: 240
            options: [
                { value: "", text: qsTr("None") },
                { value: "compose:ralt", text: qsTr("Right Alt") },
                { value: "compose:rctrl", text: qsTr("Right Ctrl") },
                { value: "compose:caps", text: "Caps Lock" },
                { value: "compose:menu", text: qsTr("Menu") },
                { value: "compose:rwin", text: qsTr("Right Super") }
            ]
        }
    }

    SettingsGroup {
        NavRow {
            label: qsTr("Keyboard Shortcuts")
            glyph: "\u{F030C}"
            onOpen: win.openSub("KeyboardShortcuts", qsTr("Keyboard Shortcuts"))
        }
        ButtonRow {
            label: qsTr("Input methods")
            sublabel: qsTr("Type Chinese, Japanese, or Korean")
            text: qsTr("Set Up…")
            onClicked: backend.act("launch", ["inputMethods"])
        }
    }
}
