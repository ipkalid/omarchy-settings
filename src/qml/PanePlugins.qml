import QtQuick
import QtQuick.Layouts

// The Omarchy shell's plugins: turn them on and off, update the ones that
// came from git, remove them, or add one from a git URL. Everything goes
// through `omarchy plugin`, as the shell's own plugin menu does.
SettingsPage {
    id: page
    pane: "plugins"
    title: qsTr("Plugins")
    tagline: qsTr("%n enabled", "", page.plugins.filter(p => p.enabled).length)
    glyph: "\u{F0431}"
    description: qsTr("Plugins add widgets to the bar and features to the shell.")
    lists: ["plugins"]

    readonly property var plugins: backend.lists.plugins || []
    readonly property var updates: backend.lists.pluginUpdates || ({})
    readonly property bool checking: backend.busy.indexOf("list:pluginUpdates") >= 0
    readonly property var updatable: plugins.filter(p => page.updates[p.id] === "available")
    readonly property string query: filter.text.trim().toLowerCase()
    function matches(p) {
        return query.length === 0 || (p.name + " " + p.id + " " + (p.description || "")).toLowerCase().indexOf(query) >= 0
    }
    readonly property var mine: plugins.filter(p => !p.firstParty && matches(p))
    readonly property var builtIn: plugins.filter(p => p.firstParty && matches(p))
    function busyWith(id) {
        return ["plugin.enable", "plugin.disable", "plugin.update", "plugin.remove"].some(a => backend.busy.indexOf(a) >= 0)
    }
    function kindNames(kinds) {
        const names = { "bar-widget": qsTr("Bar widget"), "bar": qsTr("Bar"), "service": qsTr("Service"),
                        "overlay": qsTr("Overlay"), "panel": qsTr("Panel"), "menu": qsTr("Menu") }
        return (kinds || []).map(k => names[k] || k).join(", ")
    }

    component PluginRow: SettingRow {
        id: prow
        property var plugin
        label: plugin.name + (plugin.version ? "  " + plugin.version : "")
        sublabel: [plugin.description, page.kindNames(plugin.kinds)].filter(s => !!s).join(" · ")
        dimmed: !plugin.enabled
        busy: page.busyWith(plugin.id)

        OmaButton {
            visible: page.updates[prow.plugin.id] === "available"
            text: qsTr("Update")
            highlighted: true
            enabled: !prow.busy
            anchors.verticalCenter: parent.verticalCenter
            onClicked: backend.act("plugin.update", [prow.plugin.id], ["plugins", "pluginUpdates"])
        }
        OmaButton {
            visible: !!prow.plugin.removable
            glyph: "\u{F01B4}"
            bordered: false
            tooltip: qsTr("Remove")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: { removeDialog.plugin = prow.plugin; removeDialog.open() }
        }
        OmaToggle {
            checked: !!prow.plugin.enabled
            interactive: prow.plugin.canDisable !== false && !prow.busy
            opacity: interactive ? 1 : 0.5
            onToggled: backend.act(prow.plugin.enabled ? "plugin.disable" : "plugin.enable", [prow.plugin.id], ["plugins", "bar"])
            Accessible.role: Accessible.CheckBox
            Accessible.name: prow.plugin.name
            Accessible.checked: checked
        }
    }

    SettingsGroup {
        title: qsTr("Updates")
        footer: qsTr("Only plugins installed from git can be updated here; built-in ones update with Omarchy.")
        ButtonRow {
            label: page.checking ? qsTr("Checking…")
                 : !page.updates.checkedAt ? qsTr("Check your plugins for new versions")
                 : page.updatable.length > 0 ? qsTr("%n updates available", "", page.updatable.length)
                 : qsTr("All plugins are up to date")
            text: qsTr("Check Now")
            busy: page.checking
            onClicked: backend.load("pluginUpdates")
        }
        ButtonRow {
            visible: page.updatable.length > 0
            label: page.updatable.map(p => p.name).join(", ")
            text: qsTr("Update All")
            highlighted: true
            busy: backend.busy.indexOf("plugin.updateAll") >= 0
            onClicked: backend.act("plugin.updateAll", [], ["plugins", "pluginUpdates"])
        }
    }

    OmaField {
        id: filter
        Layout.fillWidth: true
        placeholderText: qsTr("Filter %n plugins", "", page.plugins.length)
    }

    SettingsGroup {
        title: qsTr("Installed")
        footer: page.mine.length === 0 && page.query.length === 0 ? qsTr("Plugins you add show up here.") : ""
        Repeater {
            model: page.mine
            delegate: PluginRow { required property var modelData; plugin: modelData }
        }
    }

    SettingsGroup {
        title: qsTr("Built In")
        Repeater {
            model: page.builtIn
            delegate: PluginRow { required property var modelData; plugin: modelData }
        }
    }

    SettingsGroup {
        title: qsTr("Add a Plugin")
        footer: qsTr("Only add plugins you trust: they run inside the shell with your permissions.")
        SettingRow {
            label: qsTr("From a git URL")
            OmaField {
                id: url
                width: Math.round(280 * ui.scale)
                placeholderText: "https://github.com/…"
                onAccepted: add.clicked()
            }
            OmaButton {
                id: add
                text: qsTr("Add")
                enabled: url.text.trim().length > 0 && backend.busy.indexOf("plugin.add") < 0
                onClicked: backend.act("plugin.add", [url.text.trim()], ["plugins", "bar"])
            }
        }
    }

    Connections {
        target: backend
        function onActionFinished(action, error) {
            if (action === "plugin.add" && error.length === 0) url.text = ""
        }
    }

    OmaDialog {
        id: removeDialog
        property var plugin: ({ name: "", id: "" })
        title: qsTr("Remove “%1”?").arg(plugin.name)
        acceptText: qsTr("Remove")
        onAccepted: backend.act("plugin.remove", [plugin.id], ["plugins", "bar"])
        Text {
            Layout.fillWidth: true
            text: qsTr("Its folder in ~/.config/omarchy/plugins is deleted. To use it again, add it again.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
