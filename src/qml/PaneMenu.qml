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
    function childrenOf(section) {
        return items.filter(i => i.id.indexOf(section.id + ".") === 0
            && (query.length === 0 || (i.label + " " + i.id).toLowerCase().indexOf(query) >= 0))
    }
    function toggle(item) { backend.act(item.hidden ? "menu.show" : "menu.hide", [item.id], ["menu"]) }

    component MenuItemRow: SettingRow {
        id: mrow
        property var item
        readonly property string coveredBy: page.hiddenBy(item.id)
        label: item.label
        sublabel: !item.editable ? qsTr("Set over several lines in your menu file; change it there")
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
                onExpand: {
                    const next = Object.assign({}, page.expanded)
                    next[group.modelData.id] = !group.open
                    page.expanded = next
                }
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
                model: group.open ? group.children_ : []
                delegate: Item {
                    required property var modelData
                    width: parent.width
                    implicitHeight: row.implicitHeight
                    MenuItemRow {
                        id: row
                        item: modelData
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
            label: qsTr("Edit the menu file")
            text: qsTr("Open…")
            onClicked: backend.act("edit", ["menu"])
        }
    }
}
