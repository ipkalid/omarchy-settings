import QtQuick
import QtQuick.Layouts

// What the Omarchy menu shows. Hiding an item writes `"when": "false"` for
// it into ~/.config/omarchy/extensions/omarchy-menu.jsonc, the way the menu
// documents it; the menu picks the change up on its own.
SettingsPage {
    id: page
    pane: "menu"
    title: qsTr("Omarchy Menu")
    tagline: qsTr("%n hidden", "", page.items.filter(i => i.hidden).length)
    glyph: "\u{F035C}"
    description: qsTr("Hide the parts of the Omarchy menu you don't use. Hiding a section hides everything in it, in search too, and showing it brings those back.")
    lists: ["menu"]

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
    function toggle(item) { backend.act(item.hidden ? "menu.show" : "menu.hide", [item.id], ["menu"]) }

    component MenuItemRow: SettingRow {
        id: mrow
        property var item
        readonly property string coveredBy: page.hiddenBy(item.id)
        label: item.label
        readonly property int inside: page.insideCount[item.id] || 0
        sublabel: expandable && !open && inside > 0 && item.depth > 0 && !coveredBy && item.editable
                 ? qsTr("%n items inside", "", inside)
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
                text: qsTr("%n items inside", "", group.children_.length)
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
        ButtonRow {
            label: qsTr("Edit the menu file")
            text: qsTr("Open…")
            onClicked: backend.act("edit", ["menu"])
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
}
