import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
        title: qsTr("Update Channel")
        footer: qsTr("Stable is tested. Release candidates arrive earlier, and edge gets every change as it lands; both can break things. Switching opens a terminal.")
        SettingRow {
            label: qsTr("Get updates from")
            ChoiceBox {
                maxWidth: Math.round(220 * ui.scale)
                model: [
                    { value: "stable", text: qsTr("Stable") },
                    { value: "rc", text: qsTr("Release candidates") },
                    { value: "edge", text: qsTr("Edge") }
                ]
                textRole: "text"
                valueRole: "value"
                currentIndex: ["stable", "rc", "edge"].indexOf(page.about.channel || "stable")
                onActivated: {
                    if (currentValue !== page.about.channel) {
                        channelDialog.channel = currentValue
                        channelDialog.open()
                    }
                    currentIndex = Qt.binding(() => ["stable", "rc", "edge"].indexOf(page.about.channel || "stable"))
                }
                Accessible.name: qsTr("Update channel")
            }
        }
    }

    OmaDialog {
        id: channelDialog
        property string channel
        title: qsTr("Switch to the %1 channel?").arg(channel)
        acceptText: qsTr("Switch")
        onAccepted: backend.act("launch", ["channel:" + channel], ["about"])
        Text {
            Layout.fillWidth: true
            text: channelDialog.channel === "stable" ? qsTr("Your system moves back to tested releases at the next update.")
                  : qsTr("You'll get changes before they are fully tested. Switch back to stable at any time.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
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
