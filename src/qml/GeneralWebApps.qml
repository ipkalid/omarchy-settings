import QtQuick
import QtQuick.Layouts

// Web apps: sites that open in their own window and sit in the launcher,
// made and removed with omarchy webapp.
SettingsPage {
    id: page
    pane: ""
    lists: ["webapps"]

    readonly property var apps: backend.lists.webapps || []

    SettingsGroup {
        separated: false
        title: qsTr("Your Web Apps")
        footer: page.apps.length === 0 ? qsTr("Web apps you add show up here and in the app launcher.") : ""
        Repeater {
            model: page.apps
            delegate: SettingRow {
                required property var modelData
                label: modelData.title || modelData.name
                sublabel: modelData.url
                glyph: "\u{F059F}"
                OmaButton {
                    glyph: "\u{F01B4}"
                    bordered: false
                    tooltip: qsTr("Remove")
                    onClicked: backend.act("webapp.remove", [modelData.name], ["webapps"])
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Add a Web App")
        footer: qsTr("The icon is the web address of a PNG; dashboardicons.com has most sites. Leave it empty for a plain globe.")
        SettingRow {
            label: qsTr("Name")
            OmaField { id: name; width: Math.round(280 * ui.scale); placeholderText: qsTr("Calendar") }
        }
        SettingRow {
            label: qsTr("Address")
            OmaField { id: url; width: Math.round(280 * ui.scale); placeholderText: "https://…" }
        }
        SettingRow {
            label: qsTr("Icon")
            OmaField { id: icon; width: Math.round(280 * ui.scale); placeholderText: "https://…/icon.png" }
        }
        ButtonRow {
            label: ""
            text: qsTr("Add Web App")
            highlighted: true
            busy: backend.busy.indexOf("webapp.add") >= 0
            enabled: name.text.trim().length > 0 && url.text.trim().length > 0
            onClicked: backend.act("webapp.add", [name.text.trim(), url.text.trim(), icon.text.trim()], ["webapps"])
        }
    }

    Connections {
        target: backend
        function onActionFinished(action, error) {
            if (action === "webapp.add" && error.length === 0) name.text = url.text = icon.text = ""
        }
    }
}
