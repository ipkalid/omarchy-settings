import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "general"
    lists: ["about"]

    readonly property var about: backend.lists.about || ({})
    readonly property var updates: backend.lists.updates || ({})
    readonly property bool checking: backend.busy.indexOf("list:updates") >= 0

    SettingsGroup {
        footer: qsTr("Updates run in a terminal so you can follow along and answer any questions. Omarchy updates the system, its apps, and itself together.")
        InfoRow {
            label: qsTr("Omarchy")
            value: page.about.omarchy || ""
            sublabel: page.about.channel ? qsTr("%1 channel").arg(page.about.channel) : ""
        }
        ButtonRow {
            label: page.checking ? qsTr("Checking…")
                 : !page.updates.checked ? qsTr("Check whether an update is waiting")
                 : page.updates.available ? qsTr("An update is available")
                 : qsTr("Omarchy is up to date")
            sublabel: page.updates.checked && page.updates.available ? (page.updates.text || "") : ""
            text: qsTr("Check Now")
            busy: page.checking
            onClicked: backend.load("updates")
        }
        ButtonRow {
            label: qsTr("Update the system")
            text: qsTr("Update Now")
            highlighted: page.updates.available === true
            onClicked: backend.act("launch", ["update"])
        }
    }

    SettingsGroup {
        ButtonRow {
            label: qsTr("Firmware")
            sublabel: qsTr("Updates for your computer's hardware, from fwupd")
            text: qsTr("Update Firmware")
            onClicked: backend.act("launch", ["firmware"])
        }
    }
}
