import QtQuick
import QtQuick.Layouts

// What the Omarchy menu shows. Hiding an item writes `"when": "false"` for
// it into ~/.config/omarchy/extensions/omarchy-menu.jsonc, the way the menu
// documents it; the menu picks the change up on its own.
SettingsPage {
    id: page
    configFiles: ["menu"]
    pane: "menu"
    title: qsTr("Omarchy Menu")
    tagline: qsTr("%n hidden", "", page.items.filter(i => i.hidden).length)
    glyph: "\u{F035C}"
    description: qsTr("Hide the parts of the Omarchy menu you don't use. Hiding a section hides everything in it, in search too, and showing it brings those back.")
    lists: ["menu", "menuCustom"]

    readonly property var items: backend.lists.menu || []
    readonly property var sections: items.filter(i => i.depth === 0)
    readonly property string query: filter.text.trim().toLowerCase()
    readonly property bool working: ["menu.hide", "menu.show", "menu.hideInside"].some(a => backend.busy.indexOf(a) >= 0)
    // Items inside a hidden section that are not hidden themselves: the
    // menu's search still finds them.
    readonly property var searchable: items.filter(i => !i.hidden && hiddenBy(i.id))
    property var expanded: ({})

    readonly property var hiddenIds: {
        const set = {}
        for (const i of items) if (i.hidden) set[i.id] = true
        return set
    }
    // The nearest hidden section above an item, which hides it too.
    function hiddenBy(id) {
        const parts = id.split(".")
        for (let n = 1; n < parts.length; ++n) {
            const ancestor = parts.slice(0, n).join(".")
            if (hiddenIds[ancestor]) return ancestor
        }
        return ""
    }
    function labelOf(id) { const i = items.find(x => x.id === id); return i ? i.label : id }
    // A section's items in tree order: each item followed by what is inside
    // it, siblings in the order the menu file lists them (it lists all the
    // nested ones last, which would put them away from their parent).
    function childrenOf(section) {
        const under = {}
        for (const i of items) {
            if (i.id.indexOf(section.id + ".") !== 0) continue
            const parent = i.id.split(".").slice(0, -1).join(".")
            ;(under[parent] = under[parent] || []).push(i)
        }
        const ordered = []
        const walk = id => { for (const child of (under[id] || [])) { ordered.push(child); walk(child.id) } }
        walk(section.id)
        return ordered.filter(i => query.length === 0 || (i.label + " " + i.id).toLowerCase().indexOf(query) >= 0)
    }
    // How many items sit under each item, so any item with some inside
    // can open and close, at every level.
    readonly property var insideCount: {
        const counts = {}
        for (const i of items) {
            const parts = i.id.split(".")
            for (let n = 1; n < parts.length; ++n) {
                const ancestor = parts.slice(0, n).join(".")
                counts[ancestor] = (counts[ancestor] || 0) + 1
            }
        }
        return counts
    }
    // How many of those are on: not hidden themselves, and not inside
    // something hidden below the item they're counted under.
    readonly property var shownCount: {
        const counts = {}
        for (const i of items) {
            const parts = i.id.split(".")
            let on = !i.hidden
            for (let n = parts.length - 1; n >= 1; --n) {
                const ancestor = parts.slice(0, n).join(".")
                if (on) counts[ancestor] = (counts[ancestor] || 0) + 1
                if (hiddenIds[ancestor]) on = false
            }
        }
        return counts
    }
    // "12 items inside · 9 shown"; a hidden item's count of shown ones would
    // say nothing true, so it only says what's inside.
    function insideSummary(id) {
        const inside = qsTr("%n items inside", "", insideCount[id] || 0)
        return hiddenIds[id] || hiddenBy(id) ? inside : inside + " · " + qsTr("%n shown", "", shownCount[id] || 0)
    }
    property bool customOpen: false
    function isOpen(id) { return query.length > 0 || !!expanded[id] }
    function toggleOpen(id) {
        const next = Object.assign({}, expanded)
        next[id] = !isOpen(id)
        expanded = next
    }
    // An item shows when every item above it, inside its section, is open.
    function shownInTree(item) {
        const parts = item.id.split(".")
        for (let n = 2; n < parts.length; ++n)
            if (!isOpen(parts.slice(0, n).join("."))) return false
        return true
    }
    readonly property var custom: backend.lists.menuCustom || []
    // Moving Settings in the menu rewrites the file; show the result once it's written.
    readonly property bool placing: backend.busy.indexOf("integration.menuPlace") >= 0
    onPlacingChanged: if (!placing) { backend.load("menu"); backend.load("menuCustom") }
    // Where an item can go: the top of the menu, or any submenu (anything
    // with items in it, and your own submenus).
    readonly property var places: {
        const out = [{ value: "", text: qsTr("Top of the menu") }]
        const isPlace = i => (insideCount[i.id] || 0) > 0 || custom.some(c => c.id === i.id && !c.action)
        for (const i of items)
            if (isPlace(i))
                out.push({ value: i.id, text: pathOf(i.id) })
        return out
    }
    function pathOf(id) {
        const parts = id.split(".")
        return parts.map((_, n) => labelOf(parts.slice(0, n + 1).join("."))).join(" › ")
    }
    function toggle(item) { backend.act(item.hidden ? "menu.show" : "menu.hide", [item.id], ["menu"]) }

    component MenuItemRow: SettingRow {
        id: mrow
        property var item
        readonly property string coveredBy: page.hiddenBy(item.id)
        label: item.label
        readonly property int inside: page.insideCount[item.id] || 0
        sublabel: expandable && !open && inside > 0 && item.depth > 0 && !coveredBy && item.editable
                 ? page.insideSummary(item.id)
                 : !item.editable ? qsTr("Set over several lines in your menu file; change it there")
                 : coveredBy && !item.hidden ? qsTr("Hidden with %1, but search still finds it").arg(page.labelOf(coveredBy))
                 : coveredBy ? qsTr("Hidden with %1").arg(page.labelOf(coveredBy))
                 : item.custom ? qsTr("Added by you") : ""
        glyph: item.icon
        dimmed: !!coveredBy || item.hidden
        // A section opens to show its items; the arrow sits before the
        // toggle so every toggle lines up on the edge.
        property bool expandable: false
        property bool open: false
        signal expand()
        OmaButton {
            visible: mrow.expandable
            glyph: mrow.open ? "\u{F0143}" : "\u{F0140}"
            bordered: false
            tooltip: mrow.open ? qsTr("Hide items") : qsTr("Show items")
            anchors.verticalCenter: parent.verticalCenter
            onClicked: mrow.expand()
        }
        OmaToggle {
            checked: !mrow.item.hidden
            interactive: mrow.item.editable && !mrow.coveredBy && !page.working
            opacity: interactive ? 1 : 0.5
            onToggled: page.toggle(mrow.item)
            Accessible.role: Accessible.CheckBox
            Accessible.name: mrow.item.label
            Accessible.checked: checked
        }
    }

    OmaField {
        id: filter
        objectName: "menuFilter"
        Layout.fillWidth: true
        placeholderText: qsTr("Filter %n menu items", "", page.items.length)
    }

    SettingsGroup {
        visible: page.searchable.length > 0
        ButtonRow {
            label: qsTr("%n items in hidden sections still show up in search", "", page.searchable.length)
            sublabel: qsTr("Sections hidden before Settings did this for you")
            glyph: "\u{F0349}"
            tint: ui.urgent
            text: qsTr("Hide Them Too")
            highlighted: true
            busy: page.working
            onClicked: backend.act("menu.hideInside", [""], ["menu"])
        }
    }

    // Where Settings itself opens from: at the top suits people who hide Setup.
    SettingsGroup {
        title: qsTr("This App")
        footer: qsTr("Put Settings at the top of the menu if you've hidden Setup.")
        ChoiceRow {
            setting: "integration.menuPlace"
            label: qsTr("Settings in the menu")
            glyph: "\u{F062E}"
            options: [{ value: "off", text: qsTr("Not shown") },
                      { value: "setup", text: qsTr("Under Setup") },
                      { value: "top", text: qsTr("At the top") }]
        }
        SettingRow {
            label: qsTr("Find Settings' pages in the menu search")
            sublabel: qsTr("Type “vpn” or “date” in the Omarchy menu to open that page here")
            glyph: "\u{F0349}"
            busy: backend.busy.indexOf("menu.searchEntries") >= 0
            OmaToggle {
                checked: win.menuSearchOn
                onToggled: win.setMenuSearch(!win.menuSearchOn)
                Accessible.name: qsTr("Find Settings' pages in the menu search")
            }
        }
    }

    // Your own items, wherever they sit in the menu.
    SettingsGroup {
        title: qsTr("Your Items")
        collapsible: true
        collapsed: !page.customOpen
        onToggle: page.customOpen = !page.customOpen
        summary: qsTr("%n items", "", page.custom.length)
        footer: qsTr("Items you add go into omarchy-menu.jsonc, the way Omarchy's menu takes extensions. A copy of the file is kept before each change.")
        // Stays in view while the list is folded.
        leading: ButtonRow {
            label: page.custom.length === 0 ? qsTr("Add your own commands and submenus to the menu") : ""
            text: qsTr("Add Item…")
            highlighted: true
            onClicked: itemDialog.openFor(null)
        }
        Repeater {
            model: page.custom
            delegate: SettingRow {
                id: crow
                required property var modelData
                label: modelData.label
                sublabel: [modelData.parent ? qsTr("In %1").arg(page.pathOf(modelData.parent)) : qsTr("At the top"),
                           modelData.action ? qsTr("runs %1").arg("\u2066" + modelData.action + "\u2069") : qsTr("submenu"),
                           modelData.hidden ? qsTr("hidden") : ""].filter(x => !!x).join(" · ")
                glyph: modelData.icon
                dimmed: modelData.hidden
                OmaButton {
                    visible: !crow.modelData.managed
                    glyph: "\u{F03EB}"
                    bordered: false
                    tooltip: qsTr("Edit")
                    onClicked: itemDialog.openFor(crow.modelData)
                }
                OmaButton {
                    visible: !crow.modelData.managed
                    glyph: "\u{F01B4}"
                    bordered: false
                    tooltip: qsTr("Delete")
                    onClicked: { deleteItem.item = crow.modelData; deleteItem.open() }
                }
                Text {
                    visible: crow.modelData.managed
                    text: qsTr("Set under This App")
                    color: ui.muted2
                    font.family: ui.font
                    font.pixelSize: ui.caption
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                }
            }
        }
    }

    Repeater {
        model: page.sections
        delegate: SettingsGroup {
            id: group
            required property var modelData
            readonly property var children_: page.childrenOf(modelData)
            readonly property bool open: page.query.length > 0 || !!page.expanded[modelData.id]
            readonly property bool sectionMatches: page.query.length === 0
                || (modelData.label + " " + modelData.id).toLowerCase().indexOf(page.query) >= 0
            Layout.fillWidth: true
            visible: sectionMatches || children_.length > 0

            // The section itself: its toggle, and the way into its items.
            MenuItemRow {
                item: group.modelData
                expandable: group.children_.length > 0
                open: group.open
                onExpand: page.toggleOpen(group.modelData.id)
            }
            Text {
                visible: !group.open && group.children_.length > 0
                width: parent.width
                leftPadding: ui.s(40)
                text: page.insideSummary(group.modelData.id)
                color: ui.muted2
                font.family: ui.font
                font.pixelSize: ui.caption
                horizontalAlignment: Text.AlignLeft
                textFormat: Text.PlainText
            }
            Repeater {
                model: group.open ? group.children_.filter(i => page.shownInTree(i)) : []
                delegate: Item {
                    required property var modelData
                    width: parent.width
                    implicitHeight: row.implicitHeight
                    MenuItemRow {
                        id: row
                        item: modelData
                        expandable: (page.insideCount[modelData.id] || 0) > 0
                        open: page.isOpen(modelData.id)
                        onExpand: page.toggleOpen(modelData.id)
                        x: Qt.application.layoutDirection === Qt.RightToLeft ? 0 : ui.s(24) * modelData.depth
                        width: parent.width - ui.s(24) * modelData.depth
                    }
                }
            }
        }
    }

    SettingsGroup {
        footer: qsTr("Your own entries and anything more than hiding are in omarchy-menu.jsonc. A copy of it is kept the first time each day Settings changes it.")
        ButtonRow {
            label: qsTr("Restore the default menu")
            sublabel: qsTr("Shows every hidden item again; your own entries stay")
            glyph: "\u{F006F}"
            text: qsTr("Restore Default…")
            enabled: page.items.some(i => i.hidden) && !page.working
            onClicked: restoreDialog.open()
        }
    }

    OmaDialog {
        id: restoreDialog
        title: qsTr("Show all %n hidden items?", "", page.items.filter(i => i.hidden).length)
        acceptText: qsTr("Restore Default")
        onAccepted: backend.act("menu.showAll", [""], ["menu"])
        Text {
            Layout.fillWidth: true
            text: qsTr("The menu goes back to showing everything Omarchy puts in it. Items you added yourself stay, and a copy of your menu file is saved beside it first, so your hiding can be brought back.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }

    OmaDialog {
        id: itemDialog
        objectName: "itemDialog"
        property var editing: null
        readonly property bool isCommand: kind.currentIndex === 0
        title: editing ? qsTr("Edit “%1”").arg(editing.label) : qsTr("Add a Menu Item")
        acceptText: editing ? qsTr("Save") : qsTr("Add")
        width: Math.min(parent ? parent.width - ui.s(48) : 520, Math.round(520 * ui.scale))
        canAccept: name.text.trim().length > 0 && (!isCommand || command.text.trim().length > 0)

        function openFor(item) {
            editing = item
            name.text = item ? item.label : ""
            icon.text = item ? item.icon : "\u{F0415}"
            command.text = item ? item.action : ""
            description.text = item ? item.description : ""
            kind.currentIndex = item && !item.action ? 1 : 0
            place.currentIndex = Math.max(0, page.places.findIndex(p => p.value === (item ? item.parent : "")))
            open()
            name.forceActiveFocus()
        }
        onAccepted: {
            const item = { label: name.text, icon: icon.text, action: isCommand ? command.text : "", description: description.text }
            if (editing) { item.id = editing.id; backend.act("menu.editItem", [item], ["menu", "menuCustom"]) }
            else { item.parent = place.currentValue; backend.act("menu.addItem", [item], ["menu", "menuCustom"]) }
        }

        OmaField { id: name; Layout.fillWidth: true; placeholderText: qsTr("Name, as the menu shows it") }
        ChoiceBox {
            id: place
            Layout.fillWidth: true
            maxWidth: 1000
            enabled: !itemDialog.editing
            model: page.places
            textRole: "text"
            valueRole: "value"
            Accessible.name: qsTr("Where in the menu")
        }
        ChoiceBox {
            id: kind
            Layout.fillWidth: true
            maxWidth: 1000
            model: [qsTr("Runs a command"), qsTr("Opens a submenu for more items")]
            Accessible.name: qsTr("What it does")
        }
        OmaField {
            id: command
            visible: itemDialog.isCommand
            Layout.fillWidth: true
            placeholderText: qsTr("Command, e.g. omarchy-launch-editor ~/notes")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: ui.s(6)
            OmaField {
                id: icon
                Layout.preferredWidth: ui.s(56)
                horizontalAlignment: TextInput.AlignHCenter
                font.family: ui.iconFont
                placeholderText: "?"
                maximumLength: 2
                Accessible.name: qsTr("Icon")
            }
            // A few Nerd Font glyphs to pick from; any glyph can be pasted.
            Repeater {
                model: ["\u{F0415}", "\u{F0493}", "\u{F018D}", "\u{F0379}", "\u{F059F}", "\u{F024B}", "\u{F0463}", "\u{F05B7}", "\u{F0174}"]
                delegate: OmaButton {
                    required property string modelData
                    glyph: modelData
                    bordered: false
                    selected: icon.text === modelData
                    onClicked: icon.text = modelData
                }
            }
        }
        OmaField { id: description; Layout.fillWidth: true; placeholderText: qsTr("Description (optional; shown under it and searched)") }
    }

    OmaDialog {
        id: deleteItem
        property var item: ({ id: "", label: "" })
        title: qsTr("Delete “%1”?").arg(item.label)
        acceptText: qsTr("Delete")
        onAccepted: backend.act("menu.removeItem", [{ id: item.id }], ["menu", "menuCustom"])
        Text {
            Layout.fillWidth: true
            text: (page.insideCount[deleteItem.item.id] || 0) > 0
                  ? qsTr("It's a submenu: the %n items inside it go too. A copy of your menu file is kept.", "", page.insideCount[deleteItem.item.id])
                  : qsTr("It comes out of your menu file; a copy of the file is kept.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
