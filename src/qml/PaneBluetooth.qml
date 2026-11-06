import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    pane: "bluetooth"
    title: qsTr("Bluetooth")
    tagline: !page.on ? qsTr("Radio off")
           : page.connected.length > 0 ? qsTr("%n connected", "", page.connected.length) : qsTr("Nothing connected")
    glyph: page.on ? "\u{F00AF}" : "\u{F00B2}"
    heroSetting: "bluetooth.enabled"
    description: qsTr("Connect headphones, speakers, keyboards, and other wireless devices.")
    lists: ["bluetooth"]
    pollMs: 10000

    readonly property bool on: backend.values["bluetooth.enabled"] === true
    readonly property var devices: backend.lists.bluetooth || []
    readonly property var mine: devices.filter(d => d.paired)
    readonly property var connected: mine.filter(d => d.connected)
    readonly property var nearby: devices.filter(d => !d.paired)
    readonly property bool scanning: backend.busy.indexOf("bluetooth.scan") >= 0

    function deviceGlyph(icon) {
        if (/head|audio/.test(icon)) return "\u{F02CB}"
        if (/mouse/.test(icon)) return "\u{F037D}"
        if (/keyboard/.test(icon)) return "\u{F030C}"
        if (/phone/.test(icon)) return "\u{F03F2}"
        if (/computer/.test(icon)) return "\u{F0322}"
        if (/game/.test(icon)) return "\u{F0297}"
        return "\u{F00AF}"
    }
    function busyWith() {
        return ["pair", "connect", "disconnect", "forget"].some(v => backend.busy.indexOf("bluetooth." + v) >= 0)
    }

    SettingsGroup {
        visible: page.on
        title: qsTr("Paired")
        footer: page.mine.length === 0 ? qsTr("Devices you pair show up here.") : ""
        Repeater {
            model: page.mine
            delegate: SettingRow {
                id: deviceRow
                required property var modelData
                label: modelData.name
                sublabel: (modelData.connected ? qsTr("Connected") : "")
                          + (modelData.connected && modelData.battery >= 0 ? " · " : "")
                          + (modelData.battery >= 0 ? qsTr("%1% battery").arg(modelData.battery.toLocaleString(Qt.locale(), 'f', 0)) : "")
                glyph: page.deviceGlyph(modelData.icon)
                tint: modelData.connected ? ui.accent : ui.fg
                selected: modelData.connected
                busy: page.busyWith()
                OmaButton {
                    enabled: !deviceRow.busy
                    text: modelData.connected ? qsTr("Disconnect") : qsTr("Connect")
                    onClicked: backend.act(modelData.connected ? "bluetooth.disconnect" : "bluetooth.connect",
                                           [modelData.address], ["bluetooth"])
                }
                OmaButton {
                    text: qsTr("Forget")
                    bordered: false
                    onClicked: { forgetDialog.device = modelData; forgetDialog.open() }
                }
            }
        }
    }

    SettingsGroup {
        visible: page.on
        title: qsTr("Available")
        footer: qsTr("Put the device in pairing mode, then scan.")
        Repeater {
            model: page.nearby
            delegate: SettingRow {
                required property var modelData
                label: modelData.name
                glyph: page.deviceGlyph(modelData.icon)
                clickable: true
                onActivated: backend.act("bluetooth.pair", [modelData.address], ["bluetooth"])
                OmaButton {
                    text: qsTr("Pair")
                    onClicked: backend.act("bluetooth.pair", [modelData.address], ["bluetooth"])
                }
            }
        }
        ButtonRow {
            label: page.scanning ? qsTr("Scanning…") : qsTr("Look for devices")
            text: qsTr("Scan")
            busy: page.scanning
            onClicked: backend.act("bluetooth.scan", [], ["bluetooth"])
        }
    }

    OmaDialog {
        id: forgetDialog
        property var device: ({ name: "", address: "" })
        title: qsTr("Forget “%1”?").arg(device.name)
        acceptText: qsTr("Forget")
        onAccepted: backend.act("bluetooth.forget", [device.address], ["bluetooth"])
        Text {
            Layout.fillWidth: true
            text: qsTr("You will need to pair it again to use it with this computer.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
