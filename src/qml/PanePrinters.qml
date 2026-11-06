import QtQuick
import QtQuick.Layouts

// Printers known to CUPS, and network printers it found that print without
// drivers. Setting one up by hand goes through the CUPS page.
SettingsPage {
    id: page
    pane: "printers"
    title: qsTr("Printers")
    tagline: page.printers.length > 0 ? qsTr("%n printers", "", page.printers.length) : qsTr("No printers")
    glyph: "\u{F042A}"
    description: qsTr("Pick the printer to use by default, print a test page, or set up a new one.")
    lists: ["printers"]
    pollMs: 15000

    readonly property var info: backend.lists.printers || ({})
    readonly property var printers: info.printers || []

    SettingsGroup {
        visible: page.info.installed === false || page.info.cups === false
        ButtonRow {
            label: page.info.installed === false ? qsTr("Printing isn't installed") : qsTr("The printing service isn't running")
            sublabel: qsTr("Installs CUPS and turns it on")
            text: qsTr("Install")
            highlighted: true
            onClicked: backend.act("launch", ["installCups"])
        }
    }

    SettingsGroup {
        visible: page.info.cups === true
        title: qsTr("Printers")
        footer: page.printers.length === 0
                ? qsTr("No printers yet. Network printers that work without drivers appear here on their own once they are on.")
                : qsTr("Printers marked “found on the network” work without setting them up.")
        Repeater {
            model: page.printers
            delegate: SettingRow {
                required property var modelData
                label: modelData.name.replace(/_/g, " ")
                sublabel: [modelData.isDefault ? qsTr("Default") : "",
                           modelData.added ? modelData.state : qsTr("Found on the network")].filter(s => !!s).join(" · ")
                glyph: "\u{F042A}"
                tint: modelData.isDefault ? ui.accent : ui.fg
                selected: modelData.isDefault
                OmaButton {
                    visible: !modelData.isDefault
                    text: qsTr("Make Default")
                    onClicked: backend.act("printer.default", [modelData.name], ["printers"])
                }
                OmaButton {
                    text: qsTr("Test Page")
                    bordered: false
                    onClicked: backend.act("printer.test", [modelData.name])
                }
                OmaButton {
                    visible: modelData.added
                    glyph: "\u{F01B4}"
                    bordered: false
                    tooltip: qsTr("Remove")
                    onClicked: backend.act("printer.remove", [modelData.name], ["printers"])
                }
            }
        }
        ButtonRow {
            label: qsTr("Add a printer, or change one's options")
            sublabel: qsTr("Opens the CUPS page in your browser")
            text: qsTr("Open CUPS…")
            onClicked: backend.act("printer.open", [])
        }
    }
}
