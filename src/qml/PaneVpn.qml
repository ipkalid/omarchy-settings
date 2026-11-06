import QtQuick
import QtQuick.Layouts

// VPN connections kept by NetworkManager: WireGuard and OpenVPN, each with
// a way to install what it needs and to import a configuration file.
SettingsPage {
    id: page
    pane: "vpn"
    title: qsTr("VPN")
    tagline: page.active.length > 0 ? qsTr("Connected to %1").arg(page.active.map(c => c.name).join(", ")) : qsTr("Not connected")
    glyph: "\u{F0582}"
    description: qsTr("Connect to private networks through WireGuard or OpenVPN.")
    lists: ["vpn"]
    // Installing happens in a terminal; check back for when it is done.
    pollMs: 5000

    readonly property var vpn: backend.lists.vpn || ({})
    readonly property var connections: vpn.connections || []
    readonly property var active: connections.filter(c => c.active)
    readonly property bool working: ["vpn.connect", "vpn.disconnect", "vpn.import", "vpn.delete", "vpn.credentials"]
                                    .some(a => backend.busy.indexOf(a) >= 0)

    readonly property var kinds: [
        { id: "wireguard", name: "WireGuard", installed: page.vpn.wireguard === true, launch: "installWireguard",
          package: "wireguard-tools", file: ".conf",
          note: qsTr("Import the .conf file your VPN provider or server gives you.") },
        { id: "openvpn", name: "OpenVPN", installed: page.vpn.openvpn === true, launch: "installOpenvpn",
          package: "networkmanager-openvpn", file: ".ovpn",
          note: qsTr("Import the .ovpn file your VPN provider gives you, then add your username and password if it asks for them.") }
    ]

    Repeater {
        model: page.kinds
        delegate: SettingsGroup {
            id: group
            required property var modelData
            Layout.fillWidth: true
            title: modelData.name
            footer: modelData.installed ? modelData.note
                  : qsTr("Installing opens a terminal and asks for your password.")

            ButtonRow {
                visible: page.vpn.connections !== undefined && !group.modelData.installed
                label: qsTr("%1 isn't installed").arg(group.modelData.name)
                sublabel: qsTr("Needs the %1 package").arg(group.modelData.package)
                glyph: "\u{F01DA}"
                text: qsTr("Install")
                highlighted: true
                onClicked: backend.act("launch", [group.modelData.launch])
            }

            Repeater {
                model: page.connections.filter(c => c.kind === group.modelData.id)
                delegate: SettingRow {
                    id: vpnRow
                    required property var modelData
                    label: modelData.name
                    sublabel: modelData.active ? qsTr("Connected") : ""
                    glyph: modelData.active ? "\u{F0582}" : "\u{F0317}"
                    tint: modelData.active ? ui.accent : ui.fg
                    selected: modelData.active
                    OmaButton {
                        text: vpnRow.modelData.active ? qsTr("Disconnect") : qsTr("Connect")
                        enabled: !page.working
                        onClicked: backend.act(vpnRow.modelData.active ? "vpn.disconnect" : "vpn.connect",
                                               [vpnRow.modelData.uuid], ["vpn"])
                    }
                    OmaButton {
                        visible: vpnRow.modelData.kind === "openvpn"
                        glyph: "\u{F0306}"
                        bordered: false
                        tooltip: qsTr("Username and password")
                        onClicked: { credentials.connection = vpnRow.modelData; credentials.open() }
                    }
                    OmaButton {
                        glyph: "\u{F01B4}"
                        bordered: false
                        tooltip: qsTr("Delete")
                        onClicked: { deleteDialog.connection = vpnRow.modelData; deleteDialog.open() }
                    }
                }
            }

            ButtonRow {
                visible: group.modelData.installed
                label: qsTr("Add a %1 connection").arg(group.modelData.name)
                sublabel: qsTr("From a %1 file").arg(group.modelData.file)
                glyph: "\u{F0220}"
                text: qsTr("Import…")
                busy: backend.busy.indexOf("vpn.import") >= 0
                onClicked: backend.act("vpn.import", [group.modelData.id], ["vpn"])
            }
        }
    }

    OmaDialog {
        id: credentials
        property var connection: ({ name: "", uuid: "" })
        title: qsTr("Sign in to “%1”").arg(connection.name)
        acceptText: qsTr("Save")
        onOpened: { user.text = ""; password.text = ""; user.forceActiveFocus() }
        onAccepted: backend.act("vpn.credentials", [connection.uuid, user.text, password.text], ["vpn"])
        Text {
            Layout.fillWidth: true
            text: qsTr("Saved with the connection, so connecting doesn't ask each time.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        OmaField {
            id: user
            Layout.fillWidth: true
            placeholderText: qsTr("Username")
            onAccepted: password.forceActiveFocus()
        }
        OmaField {
            id: password
            Layout.fillWidth: true
            placeholderText: qsTr("Password")
            echoMode: TextInput.Password
            onAccepted: credentials.accept()
        }
    }

    OmaDialog {
        id: deleteDialog
        property var connection: ({ name: "", uuid: "" })
        title: qsTr("Delete “%1”?").arg(connection.name)
        acceptText: qsTr("Delete")
        onAccepted: backend.act("vpn.delete", [connection.uuid], ["vpn"])
        Text {
            Layout.fillWidth: true
            text: qsTr("The connection and its saved settings are removed. You can import it again from its file.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
