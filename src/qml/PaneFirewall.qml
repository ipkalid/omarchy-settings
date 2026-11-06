import QtQuick
import QtQuick.Layouts

// The firewall (ufw): whether it is on, and which ports it lets in. Reading
// needs nothing; every change asks for your password through polkit.
SettingsPage {
    id: page
    pane: "firewall"
    title: qsTr("Firewall")
    tagline: !page.fw.installed ? qsTr("Not installed") : page.fw.enabled ? qsTr("On") : qsTr("Off")
    glyph: "\u{F0498}"
    description: qsTr("The firewall blocks connections from other computers, except to the ports you allow.")
    lists: ["firewall"]

    readonly property var fw: backend.lists.firewall || ({})
    readonly property bool working: ["firewall.enable", "firewall.disable", "firewall.allow", "firewall.delete"]
                                    .some(a => backend.busy.indexOf(a) >= 0)
    readonly property var actionNames: ({ allow: qsTr("Allowed"), deny: qsTr("Blocked"), reject: qsTr("Refused"),
                                          limit: qsTr("Allowed, slowed if hammered") })

    SettingsGroup {
        visible: page.fw.installed === false
        ButtonRow {
            label: qsTr("ufw isn't installed")
            text: qsTr("Install")
            highlighted: true
            onClicked: backend.act("launch", ["installUfw"])
        }
    }

    SettingsGroup {
        visible: page.fw.installed === true
        footer: qsTr("Turning the firewall on or off asks for your password.")
        SettingRow {
            label: qsTr("Firewall")
            sublabel: page.fw.enabled ? qsTr("Incoming connections are blocked unless allowed below")
                                      : qsTr("Anything on the network can try to connect")
            busy: page.working
            OmaToggle {
                checked: page.fw.enabled === true
                interactive: !page.working
                onToggled: backend.act(page.fw.enabled ? "firewall.disable" : "firewall.enable", [], ["firewall"])
                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Firewall")
                Accessible.checked: checked
            }
        }
    }

    SettingsGroup {
        visible: page.fw.installed === true
        title: qsTr("Allowed In")
        footer: (page.fw.rules || []).length === 0 ? qsTr("No exceptions: nothing is let in.") : ""
        Repeater {
            model: page.fw.rules || []
            delegate: SettingRow {
                required property var modelData
                label: modelData.port === "any" ? qsTr("Any port") : qsTr("Port %1").arg(modelData.port.replace(":", "–"))
                sublabel: [modelData.proto === "any" ? qsTr("TCP and UDP") : modelData.proto.toUpperCase(),
                           page.actionNames[modelData.action] || modelData.action,
                           modelData.from === "0.0.0.0/0" || modelData.from === "::/0" ? qsTr("from anywhere")
                                                                                     : qsTr("from %1").arg(modelData.from)].join(" · ")
                glyph: modelData.action === "allow" || modelData.action === "limit" ? "\u{F012C}" : "\u{F0498}"
                OmaButton {
                    glyph: "\u{F01B4}"
                    bordered: false
                    // ufw deletes a rule by repeating it; rules limited to
                    // certain addresses are left to the command line.
                    enabled: !page.working && (modelData.from === "0.0.0.0/0" || modelData.from === "::/0")
                    tooltip: qsTr("Delete")
                    onClicked: backend.act("firewall.delete", [modelData.port, modelData.proto, modelData.action], ["firewall"])
                }
            }
        }
        SettingRow {
            label: qsTr("Allow a port")
            OmaField {
                id: port
                width: Math.round(110 * ui.scale)
                placeholderText: qsTr("e.g. 22")
                validator: RegularExpressionValidator { regularExpression: /^\d{1,5}(:\d{0,5})?$/ }
            }
            ChoiceBox {
                id: proto
                maxWidth: Math.round(140 * ui.scale)
                model: [{ value: "tcp", text: "TCP" }, { value: "udp", text: "UDP" }, { value: "any", text: qsTr("Both") }]
                textRole: "text"
                valueRole: "value"
                currentIndex: 0
                Accessible.name: qsTr("Protocol")
            }
            OmaButton {
                text: qsTr("Allow")
                enabled: port.text.length > 0 && !page.working
                onClicked: { backend.act("firewall.allow", [port.text, proto.currentValue], ["firewall"]); port.text = "" }
            }
        }
    }
}
