import QtQuick
import QtQuick.Layouts

// Settings itself: how to reach it, everything it has changed (each one
// undoable), and moving settings to another computer.
SettingsPage {
    id: page
    pane: "general"
    lists: ["managed"]

    readonly property var changes: backend.lists.managed || []
    readonly property var kindNames: ({
        hypr: qsTr("Setting"), monitor: qsTr("Display"), binding: qsTr("Shortcut"), unbind: qsTr("Shortcut turned off"),
        autostart: qsTr("Opens at login"), rule: qsTr("Window rule"), gesture: qsTr("Gesture"), nightShift: qsTr("Night Shift")
    })
    function shown(value) {
        if (value === true) return qsTr("On")
        if (value === false) return qsTr("Off")
        return String(value)
    }

    SettingsGroup {
        separated: false
        title: qsTr("Opening Settings")
        ToggleRow { setting: "integration.menuEntry"; label: qsTr("Show Settings in the Omarchy menu, under Setup") }
        ButtonRow {
            label: qsTr("A keyboard shortcut for Settings")
            text: qsTr("Choose Keys…")
            onClicked: win.openSub("KeyboardShortcuts", qsTr("Keyboard Shortcuts"), { preset: "settings" })
        }
    }

    SettingsGroup {
        title: qsTr("Move to Another Computer")
        footer: qsTr("Exports your settings, shortcuts, login items, and window rules to a file. Things that belong to this machine, like its name, time zone, and sound devices, stay behind.")
        ButtonRow {
            label: qsTr("Save your settings to a file")
            text: qsTr("Export…")
            busy: backend.busy.indexOf("settings.export") >= 0
            onClicked: backend.act("settings.export", [])
        }
        ButtonRow {
            label: qsTr("Apply settings from a file")
            text: qsTr("Import…")
            busy: backend.busy.indexOf("settings.import") >= 0
            onClicked: backend.act("settings.import", [], ["managed"], "general")
        }
    }

    SettingsGroup {
        title: qsTr("Changes Made Here")
        footer: page.changes.length === 0
                ? qsTr("Nothing yet. Settings that come from Omarchy's own commands (theme, bar, and so on) aren't listed; they live where Omarchy keeps them.")
                : qsTr("These live in ~/.config/hypr/omasettings.lua. Resetting one hands it back to your own config files.")
        Repeater {
            model: page.changes
            delegate: SettingRow {
                required property var modelData
                label: qsTranslate("Setting", modelData.label)
                sublabel: page.kindNames[modelData.kind] + (String(modelData.value).length > 0 ? " · " + page.shown(modelData.value) : "")
                OmaButton {
                    text: qsTr("Reset")
                    bordered: false
                    onClicked: backend.act("managed.reset", [modelData.kind, modelData.key], ["managed"], "general")
                }
            }
        }
        ButtonRow {
            visible: page.changes.length > 1
            label: qsTr("Reset everything above")
            text: qsTr("Reset All…")
            onClicked: resetAll.open()
        }
    }

    OmaDialog {
        id: resetAll
        title: qsTr("Reset all %n changes?", "", page.changes.length)
        acceptText: qsTr("Reset All")
        onAccepted: backend.act("managed.resetAll", [], ["managed"], "general")
        Text {
            Layout.fillWidth: true
            text: qsTr("Every change listed goes back to what your own config files say. Shortcuts, login items, and window rules made here are deleted.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
