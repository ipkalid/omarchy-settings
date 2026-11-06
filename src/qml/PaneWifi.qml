import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    pane: "wifi"
    title: page.current ? page.current.ssid : qsTr("Wi-Fi")
    tagline: !page.on ? qsTr("Radio off") : page.current ? qsTr("Connected") : qsTr("Not connected")
    glyph: page.on ? "\u{F05A9}" : "\u{F05AA}"
    heroSetting: "wifi.enabled"
    description: qsTr("Join wireless networks and manage the ones this computer remembers.")
    lists: ["wifi"]
    pollMs: 15000

    readonly property bool on: backend.values["wifi.enabled"] === true
    readonly property var networks: backend.lists.wifi || []
    readonly property var current: on ? networks.find(n => n.active) : undefined
    readonly property var known: networks.filter(n => n.known)
    readonly property var others: networks.filter(n => !n.known && !n.active)
    readonly property bool working: backend.busy.indexOf("wifi.connect") >= 0 || backend.busy.indexOf("list:wifi") >= 0

    function signalGlyph(signal) {
        return signal > 75 ? "\u{F0928}" : signal > 50 ? "\u{F0925}" : signal > 25 ? "\u{F0922}" : "\u{F091F}"
    }
    function join(network) {
        if (network.active)
            return
        if (network.security && !network.known) {
            passwordDialog.ssid = network.ssid
            passwordDialog.open()
        } else {
            backend.act("wifi.connect", [network.ssid, ""], ["wifi"], "wifi")
        }
    }

    // A network as the shell's Wi-Fi panel lists it: signal, name, a lock
    // when it has a password, and the connected one highlighted.
    component NetworkRow: SettingRow {
        id: nrow
        property var network
        label: network.ssid
        sublabel: network.active ? qsTr("Connected") : ""
        glyph: page.signalGlyph(network.signal)
        tint: network.active ? ui.accent : ui.fg
        selected: !!network.active
        clickable: !network.active
        onActivated: page.join(network)
        PaneIcon {
            visible: !!nrow.network.security
            glyph: "\u{F033E}"
            tint: ui.muted
            size: ui.small
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    SettingsGroup {
        visible: page.on
        title: qsTr("Known Networks")
        Repeater {
            model: page.known
            delegate: NetworkRow {
                required property var modelData
                network: modelData
                OmaButton {
                    visible: !!modelData.active
                    text: qsTr("Disconnect")
                    onClicked: backend.act("wifi.disconnect", [], ["wifi"], "wifi")
                }
                OmaButton {
                    text: qsTr("Forget")
                    bordered: false
                    onClicked: { forgetDialog.ssid = modelData.ssid; forgetDialog.open() }
                }
            }
        }
    }

    SettingsGroup {
        visible: page.on
        title: qsTr("Other Networks")
        footer: page.others.length === 0 && !page.working ? qsTr("No other networks found") : ""
        Repeater {
            model: page.others
            delegate: NetworkRow {
                required property var modelData
                network: modelData
            }
        }
    }

    SettingsGroup {
        visible: page.on
        ButtonRow {
            label: page.working ? qsTr("Looking for networks…") : qsTr("Look for networks again")
            text: qsTr("Scan")
            busy: page.working
            onClicked: backend.act("wifi.rescan", [], ["wifi"])
        }
    }

    OmaDialog {
        id: passwordDialog
        property string ssid
        title: qsTr("Join “%1”").arg(ssid)
        acceptText: qsTr("Join")
        onOpened: { password.text = ""; password.forceActiveFocus() }
        onAccepted: backend.act("wifi.connect", [ssid, password.text], ["wifi"], "wifi")
        OmaField {
            id: password
            Layout.fillWidth: true
            placeholderText: qsTr("Password")
            echoMode: TextInput.Password
            onAccepted: passwordDialog.accept()
        }
    }

    OmaDialog {
        id: forgetDialog
        property string ssid
        title: qsTr("Forget “%1”?").arg(ssid)
        acceptText: qsTr("Forget")
        onAccepted: backend.act("wifi.forget", [ssid], ["wifi"], "wifi")
        Text {
            Layout.fillWidth: true
            text: qsTr("This computer will stop joining this network, and you will need its password to join again.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
