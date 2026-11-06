import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "desktop"
    title: qsTr("Desktop & Dock")
    tagline: ({ dwindle: qsTr("Tiling"), scrolling: qsTr("Scrolling"), floating: qsTr("Floating") })[backend.values["desktop.layout"]] || ""
    glyph: "\u{F056E}"
    description: qsTr("Place the dock, and choose how windows are laid out and focused.")

    lists: ["dock", "apps", "windowRules"]
    readonly property var pinned: backend.lists.dock || []

    function pixels(v) { return qsTr("%1 px").arg(Math.round(v).toLocaleString(Qt.locale(), 'f', 0)) }
    readonly property var sides: [
        { value: "top", text: qsTr("Top") }, { value: "bottom", text: qsTr("Bottom") },
        { value: "left", text: qsTr("Left") }, { value: "right", text: qsTr("Right") }
    ]

    SettingsGroup {
        title: qsTr("Dock")
        footer: qsTr("The dock appears on floating workspaces.")
        ChoiceRow { setting: "desktop.dockPosition"; label: qsTr("Position on screen"); options: page.sides }
        ToggleRow { setting: "desktop.dockAutohide"; label: qsTr("Automatically hide and show the dock") }
        Repeater {
            model: page.pinned
            delegate: SettingRow {
                required property var modelData
                required property int index
                label: modelData.name
                sublabel: qsTr("Pinned")
                OmaButton {
                    glyph: "\u{F0143}"
                    bordered: false
                    enabled: index > 0
                    tooltip: qsTr("Move up")
                    onClicked: {
                        const ids = page.pinned.map(p => p.id)
                        ids.splice(index - 1, 0, ids.splice(index, 1)[0])
                        backend.act("dock.order", ids, ["dock"])
                    }
                }
                OmaButton {
                    glyph: "\u{F0156}"
                    bordered: false
                    tooltip: qsTr("Unpin")
                    onClicked: backend.act("dock.unpin", [modelData.id], ["dock"])
                }
            }
        }
        SettingRow {
            label: qsTr("Pin an app")
            ChoiceBox {
                id: pinApp
                maxWidth: Math.round(240 * ui.scale)
                model: (backend.lists.apps || []).filter(a => !page.pinned.some(p => p.id === a.id))
                textRole: "name"
                valueRole: "id"
                currentIndex: -1
                placeholder: qsTr("Choose an app")
                onCountChanged: Qt.callLater(() => currentIndex = -1)
                Accessible.name: qsTr("App to pin")
            }
            OmaButton {
                text: qsTr("Pin")
                enabled: pinApp.currentIndex >= 0
                onClicked: backend.act("dock.pin", [pinApp.currentValue], ["dock"])
            }
        }
    }

    SettingsGroup {
        title: qsTr("Windows")
        footer: qsTr("Tiling fills the screen with windows side by side, scrolling lines them up in a strip you scroll through, and floating lets you place them anywhere.")
        ChoiceRow {
            setting: "desktop.layout"
            label: qsTr("Arrange new workspaces by")
            options: [
                { value: "dwindle", text: qsTr("Tiling") },
                { value: "scrolling", text: qsTr("Scrolling") },
                { value: "floating", text: qsTr("Floating") }
            ]
        }
        ToggleRow { setting: "desktop.gaps"; label: qsTr("Gaps between windows") }
        SliderRow {
            setting: "desktop.gapsInner"
            label: qsTr("Gap between windows")
            dimmed: backend.values["desktop.gaps"] === false || value === undefined
            to: 30
            format: page.pixels
        }
        SliderRow { setting: "desktop.gapsOuter"; label: qsTr("Gap at screen edges"); to: 60; format: page.pixels }
        SliderRow { setting: "desktop.borderSize"; label: qsTr("Window border"); to: 10; format: page.pixels }
        ToggleRow { setting: "desktop.dimInactive"; label: qsTr("Dim inactive windows") }
    }

    SettingsGroup {
        NavRow {
            label: qsTr("Window Rules")
            sublabel: qsTr("Open apps floating, on a workspace, at a size")
            glyph: "\u{F05B2}"
            detail: ((backend.lists.windowRules || {}).rules || []).length > 0
                    ? qsTr("%n rules", "", backend.lists.windowRules.rules.length) : ""
            onOpen: win.openSub("DesktopRules", qsTr("Window Rules"))
        }
    }

    SettingsGroup {
        title: qsTr("Focus and Pointer")
        ToggleRow {
            setting: "desktop.focusFollowsMouse"
            label: qsTr("Focus follows the mouse")
            sublabel: qsTr("Windows take the keyboard when the pointer moves over them, without a click")
        }
        ToggleRow {
            setting: "desktop.cursorWarps"
            label: qsTr("Move the pointer to focused windows")
            sublabel: qsTr("When you switch windows from the keyboard")
        }
        ChoiceRow {
            setting: "desktop.hideCursor"
            label: qsTr("Hide the pointer when idle")
            options: [
                { value: 0, text: qsTr("Never") },
                { value: 3, text: qsTr("After %n seconds", "", 3) },
                { value: 5, text: qsTr("After %n seconds", "", 5) },
                { value: 10, text: qsTr("After %n seconds", "", 10) },
                { value: 30, text: qsTr("After %n seconds", "", 30) }
            ]
        }
    }
}
