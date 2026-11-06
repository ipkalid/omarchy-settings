import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    pane: ""
    lists: ["keybindings"]

    readonly property var bindings: backend.lists.keybindings || []
    readonly property var shown: {
        const q = filter.text.trim().toLowerCase()
        return q.length === 0 ? bindings
             : bindings.filter(b => (b.keys + " " + b.action).toLowerCase().indexOf(q) >= 0)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        OmaField {
            id: filter
            Layout.fillWidth: true
            placeholderText: qsTr("Filter %n shortcuts", "", page.bindings.length)
        }
        OmaButton {
            text: qsTr("Edit Shortcuts…")
            onClicked: backend.act("edit", ["bindings"])
            tooltip: qsTr("Opens ~/.config/hypr/bindings.lua in your editor")
        }
    }

    SettingsGroup {
        footer: qsTr("Your own shortcuts live in bindings.lua. Saving it applies them right away.")
        Repeater {
            model: page.shown
            delegate: SettingRow {
                required property var modelData
                label: modelData.action
                Text {
                    text: modelData.keys
                    color: ui.accent
                    font.family: ui.iconFont
                    font.pixelSize: ui.small
                    textFormat: Text.PlainText
                }
            }
        }
    }
}
