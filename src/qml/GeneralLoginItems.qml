import QtQuick
import QtQuick.Layouts

// Apps and commands that start when you log in.
SettingsPage {
    id: page
    configFiles: ["autostart"]
    pane: ""
    lists: ["loginItems", "apps"]

    readonly property var items: (backend.lists.loginItems || {}).managed || []
    readonly property var fromFile: (backend.lists.loginItems || {}).fromFile || []

    SettingsGroup {
        separated: false
        title: qsTr("Open at Login")
        footer: page.items.length === 0 ? qsTr("Nothing yet. Items start the next time you log in.")
                                        : qsTr("Items start the next time you log in.")
        Repeater {
            model: page.items
            delegate: SettingRow {
                required property var modelData
                label: modelData.name || modelData.command
                sublabel: modelData.name && modelData.name !== modelData.command ? modelData.command : ""
                glyph: "\u{F0463}"
                OmaButton {
                    glyph: "\u{F01B4}"
                    bordered: false
                    tooltip: qsTr("Remove")
                    onClicked: backend.act("autostart.remove", [modelData.command], ["loginItems"])
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Add")
        SettingRow {
            label: qsTr("An app")
            ChoiceBox {
                id: app
                maxWidth: Math.round(240 * ui.scale)
                model: backend.lists.apps || []
                textRole: "name"
                valueRole: "id"
                currentIndex: -1
                placeholder: qsTr("Choose an app")
                onCountChanged: Qt.callLater(() => currentIndex = -1)
                Accessible.name: qsTr("App to open at login")
            }
            OmaButton {
                text: qsTr("Add")
                enabled: app.currentIndex >= 0
                // uwsm starts an app from its desktop entry by id.
                onClicked: backend.act("autostart.add", [app.currentValue + ".desktop", app.currentText], ["loginItems"])
            }
        }
        SettingRow {
            label: qsTr("A command")
            OmaField { id: command; width: Math.round(240 * ui.scale); placeholderText: qsTr("e.g. syncthing") }
            OmaButton {
                text: qsTr("Add")
                enabled: command.text.trim().length > 0
                onClicked: { backend.act("autostart.add", [command.text.trim(), ""], ["loginItems"]); command.text = "" }
            }
        }
    }

    SettingsGroup {
        visible: page.fromFile.length > 0
        title: qsTr("From autostart.lua")
        footer: qsTr("These are in your own ~/.config/hypr/autostart.lua; edit that file to change them.")
        Repeater {
            model: page.fromFile
            delegate: SettingRow { required property string modelData; label: modelData; dimmed: true }
        }
    }
}
