import QtQuick
import QtQuick.Layouts

// Everything Omarchy's Install and Remove menus offer, so hiding those menus
// costs nothing. The list comes from Omarchy's own menu, what's installed
// is what the menu itself checks, and each button runs the menu's own
// command, most of which open a terminal to finish.
SettingsPage {
    id: page
    pane: "software"
    title: qsTr("Apps")
    tagline: qsTr("%n installed", "", page.items.filter(i => i.installed && i.category !== "tools").length)
    glyph: "\u{F003B}"
    description: qsTr("Install and remove the apps and tools Omarchy offers, even with the menu's Install and Remove hidden.")
    lists: ["software"]
    // Installs finish in a terminal after the click; look again for a while.
    pollMs: watching ? 6000 : 0

    property bool watching: false
    Timer { id: stopWatching; interval: 180000; onTriggered: page.watching = false }

    readonly property var items: backend.lists.software || []
    // The page's own filter, or else the sidebar's search when it names apps
    // (not when it found the pane by a word like "install").
    function matches(item, q) { return (item.label + " " + item.key + " " + (item.package || "")).toLowerCase().indexOf(q) >= 0 }
    readonly property string sidebarQuery: win.needle.length > 1
        && items.some(i => i.category !== "tools" && matches(i, win.needle)) ? win.needle : ""
    readonly property string query: filter.text.trim().toLowerCase() || sidebarQuery
    property bool installedOnly: false
    function shown(item) {
        if (page.installedOnly && !item.installed) return false
        return query.length === 0 || matches(item, query)
    }
    readonly property var categories: [
        { id: "browser", title: qsTr("Browsers") },
        { id: "ai", title: qsTr("AI") },
        { id: "agent", title: qsTr("Coding Agents") },
        { id: "service", title: qsTr("Services") },
        { id: "editor", title: qsTr("Editors") },
        { id: "terminal", title: qsTr("Terminals") },
        { id: "gaming", title: qsTr("Gaming") },
        { id: "development", title: qsTr("Development") },
        { id: "dictation", title: qsTr("Dictation") }
    ]
    // The menu's names for its top-level tools are terse; these say what they open.
    readonly property var toolText: ({
        "package": { label: qsTr("Packages"), detail: qsTr("Pick packages from Arch's repositories") },
        "aur": { label: qsTr("AUR packages"), detail: qsTr("Pick packages from the Arch User Repository") },
        "tui": { label: qsTr("Terminal apps"), detail: qsTr("Add a terminal app to the app launcher") },
        "windows": { label: qsTr("Windows"), detail: qsTr("Windows in a virtual machine") },
        "preinstalls": { label: qsTr("Preinstalled apps"), detail: qsTr("The apps Omarchy comes with") }
    })

    // Which groups are open. Categories start closed so the page reads as a
    // list of them; filtering opens everything so matches show.
    property var openGroups: ({ "tools": true })
    readonly property bool searching: query.length > 0 || installedOnly || win.query.trim().length > 1
    function isOpen(id) { return searching || !!openGroups[id] }
    function toggleGroup(id) {
        const next = Object.assign({}, openGroups)
        next[id] = !openGroups[id]
        openGroups = next
    }

    function run(id) {
        backend.act("software.run", [id], ["software"])
        page.watching = true
        stopWatching.restart()
    }

    component AppRow: SettingRow {
        id: arow
        property var item
        readonly property bool known: !!item.known
        readonly property bool canInstall: !!item.installId && item.available && (!known || !item.installed)
        readonly property bool canRemove: (!!item.removeId || !!item.dropRemove) && (!known || !!item.installed)
        readonly property var tool: page.toolText[item.key]

        label: tool ? tool.label : item.label
        glyph: item.icon || ""
        glyphFamily: item.iconFont || ""
        dimmed: !item.available
        tint: item.installed && item.category !== "tools" ? ui.accent : ui.fg
        sublabel: {
            if (tool) return tool.detail
            if (!item.available) return qsTr("Not available on this computer")
            if (item.installed) {
                if (item.defaultKind === "terminal") return qsTr("Installed · your terminal")
                if (item.defaultKind === "editor") return qsTr("Installed · your editor")
                if (item.defaultKind === "browser") return qsTr("Installed · your browser")
                if (item.defaultKind === "agent") return qsTr("Installed · your coding agent")
                return qsTr("Installed", "status")
            }
            if (item.category === "dictation") return qsTr("Installing it makes it your dictation")
            if (item.category === "agent") return qsTr("Installing it makes it your coding agent")
            return known ? qsTr("Not installed", "status") : ""
        }

        OmaButton {
            visible: arow.canInstall
            text: qsTr("Install…")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: page.run(arow.item.installId)
        }
        OmaButton {
            visible: arow.canRemove
            text: qsTr("Remove…")
            bordered: false
            anchors.verticalCenter: parent.verticalCenter
            onClicked: { removeDialog.item = arow.item; removeDialog.open() }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: ui.s(12)
        OmaField {
            id: filter
            Layout.fillWidth: true
            placeholderText: qsTr("Filter %n apps", "", page.items.filter(i => i.category !== "tools").length)
        }
        Text {
            text: qsTr("Installed only")
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            textFormat: Text.PlainText
        }
        OmaToggle {
            checked: page.installedOnly
            onToggled: page.installedOnly = !page.installedOnly
            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("Installed only")
            Accessible.checked: checked
        }
    }

    SettingsGroup {
        visible: page.query.length === 0 && !page.installedOnly
        title: qsTr("Packages & Tools")
        footer: qsTr("Each opens a terminal to pick what to add or remove.")
        collapsible: true
        collapsed: !page.isOpen("tools")
        onToggle: page.toggleGroup("tools")
        Repeater {
            model: page.items.filter(i => i.category === "tools")
            delegate: AppRow { required property var modelData; item: modelData }
        }
    }

    Repeater {
        model: page.categories
        delegate: SettingsGroup {
            id: group
            required property var modelData
            readonly property var matching: page.items.filter(i => i.category === modelData.id && page.shown(i))
            readonly property int installedCount: page.items.filter(i => i.category === modelData.id && i.installed).length
            readonly property int total: page.items.filter(i => i.category === modelData.id).length
            visible: matching.length > 0
            title: modelData.title
            collapsible: true
            collapsed: !page.isOpen(modelData.id)
            summary: qsTr("%1 of %n installed", "", total).arg(installedCount)
            onToggle: page.toggleGroup(modelData.id)
            Repeater {
                model: group.matching
                delegate: AppRow { required property var modelData; item: modelData }
            }
        }
    }

    Text {
        visible: page.items.length > 0 && page.categories.every(c => !page.items.some(i => i.category === c.id && page.shown(i)))
        Layout.fillWidth: true
        text: qsTr("Nothing matches.")
        color: ui.muted
        font.family: ui.font
        font.pixelSize: ui.small
        horizontalAlignment: Text.AlignLeft
        textFormat: Text.PlainText
    }

    SettingsGroup {
        visible: page.query.length === 0 && !page.installedOnly
        title: qsTr("Elsewhere in Settings")
        collapsible: true
        collapsed: !page.isOpen("elsewhere")
        onToggle: page.toggleGroup("elsewhere")
        footer: qsTr("The menu's other Install and Remove items have a place of their own.")
        NavRow {
            label: qsTr("Fonts")
            glyph: "\u{F06D6}"
            onOpen: win.go("fonts")
        }
        NavRow {
            label: qsTr("Themes")
            glyph: "\u{F03D8}"
            onOpen: win.go("appearance", "AppearanceThemes", qsTr("Themes"))
        }
        NavRow {
            label: qsTr("Wallpaper")
            glyph: "\u{F02E9}"
            onOpen: win.go("wallpaper")
        }
        NavRow {
            label: qsTr("Web Apps")
            glyph: "\u{F059F}"
            onOpen: win.go("general", "GeneralWebApps", qsTr("Web Apps"))
        }
        NavRow {
            label: qsTr("Fingerprint, security keys, and SSH")
            glyph: "\u{F0237}"
            onOpen: win.go("security")
        }
    }

    OmaDialog {
        id: removeDialog
        objectName: "softwareRemove"
        property var item: ({ label: "", key: "" })
        readonly property bool drop: !item.removeId && !!item.dropRemove
        title: qsTr("Remove %1?").arg(page.toolText[item.key] ? page.toolText[item.key].label : item.label)
        acceptText: qsTr("Remove")
        onAccepted: {
            if (drop) {
                backend.act("software.drop", [item.key], ["software"])
                page.watching = true
                stopWatching.restart()
            } else {
                page.run(item.removeId)
            }
        }
        Text {
            Layout.fillWidth: true
            text: removeDialog.drop
                  ? qsTr("A terminal opens to remove the %1 package, and asks for your password.").arg(removeDialog.item.package || "")
                  : qsTr("A terminal opens to remove it the way Omarchy's menu does, and may ask for your password.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        Text {
            visible: removeDialog.item.defaultKind === "browser"
            Layout.fillWidth: true
            text: qsTr("It's your default browser. Pick another under General › Default Apps so links still open.")
            horizontalAlignment: Text.AlignLeft
            color: ui.urgent
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
