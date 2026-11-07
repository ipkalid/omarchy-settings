import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    resetPane: "displays"
    pane: "displays"
    title: qsTr("Displays")
    tagline: page.monitors.length > 0 ? qsTr("%n displays", "", page.enabled.length) : ""
    glyph: "\u{F0379}"
    lists: ["monitors", "hardware"]

    readonly property var monitors: backend.lists.monitors || []
    readonly property var enabled: monitors.filter(m => !m.disabled)
    // Numbered in the order Hyprland found them, so a number stays with its
    // display while it is moved around; the same here, in the arrangement,
    // and on the screens when identifying them.
    readonly property var numbered: enabled.slice().sort((a, b) => (a.id - b.id) || a.name.localeCompare(b.name))
    // Displays with a picture of their own, which have a place in the layout.
    readonly property var arranged: numbered.filter(m => !m.mirrorOf)
    property string selectedKey: ""
    // A display that is turned off has nothing to set but Turn On, so the
    // choice falls back to one that is on.
    readonly property var selected: enabled.find(m => m.key === selectedKey) || enabled.find(m => m.focused) || enabled[0] || monitors[0]
    readonly property bool applying: backend.busy.indexOf("monitors.apply") >= 0 || backend.busy.indexOf("monitors.restore") >= 0
    readonly property bool multiple: enabled.length > 1

    function label(m) { return m ? (m.model || m.name) : "" }
    function fullName(m) { return m ? (m.make ? m.make + " " + m.model : m.name) : "" }
    function numberOf(m) { return numbered.findIndex(o => o.key === m.key) + 1 }
    // The size a display takes up in the layout: its mode, turned when
    // rotated, divided by its scale.
    function logical(m) {
        const turned = m.transform % 2 === 1
        return { w: Math.round((turned ? m.height : m.width) / m.scale), h: Math.round((turned ? m.width : m.height) / m.scale) }
    }
    // The display a mirror shows, by Settings' own record or hyprctl's.
    function mirrorTarget(m) {
        const saved = (m.saved || {}).mirror
        return enabled.find(o => o.key !== m.key && (o.key === saved || o.name === m.mirrorOf))
    }

    readonly property var resolutions: {
        if (!selected) return []
        const seen = []
        for (const m of selected.modes) {
            const r = m.split("@")[0]
            if (seen.indexOf(r) < 0) seen.push(r)
        }
        return seen.map(r => ({ value: r, text: r.replace("x", " × ") }))
    }
    readonly property string resolution: selected ? selected.width + "x" + selected.height : ""
    readonly property var rates: {
        if (!selected) return []
        const out = []
        for (const m of selected.modes) {
            const [r, hz] = m.split("@")
            if (r === resolution && out.findIndex(o => o.value === hz) < 0)
                out.push({ value: hz, text: qsTr("%1 Hz").arg(Number(hz).toLocaleString(Qt.locale(), 'f', 2)) })
        }
        return out
    }
    readonly property var scales: [1, 1.25, 1.333333, 1.5, 1.6, 1.666667, 1.75, 2, 2.5, 3].map(s => ({
        value: s, text: qsTr("%1%").arg(Math.round(s * 100).toLocaleString(Qt.locale(), 'f', 0)) }))

    // A display's whole entry as it is now: what Settings saved for it
    // (so choices like adaptive sync carry over), with the mode, place,
    // scale, and rotation it has.
    function configFor(m) {
        const c = Object.assign({}, m.saved || {})
        delete c.disabled
        c.mode = m.width + "x" + m.height + "@" + Number(m.refreshRate).toFixed(2)
        c.position = m.x + "x" + m.y
        c.scale = m.scale
        c.transform = m.transform
        return c
    }
    // What to put back for `list` if a change is undone: what was saved, or
    // null for nothing.
    function previousOf(list) {
        const out = {}
        for (const m of list)
            out[m.key] = backend.savedMonitor(m.key, m.name) || null
        return out
    }

    // Changing a display can leave it showing nothing, so like macOS the
    // change is undone unless it is confirmed in time. A null in `changes`
    // takes that choice out.
    property var pending: null
    function apply(changes) {
        const m = selected
        const config = configFor(m)
        for (const k in changes) {
            if (changes[k] === null) delete config[k]
            else config[k] = changes[k]
        }
        const map = {}
        map[m.key] = config
        pending = { previous: previousOf([m]) }
        backend.act("monitors.apply", [map], ["monitors"])
    }
    function nearest(list, value) {
        let best = null
        for (const o of list)
            if (best === null || Math.abs(o.value - value) < Math.abs(best.value - value)) best = o
        return best ? best.value : value
    }

    // Moving displays can't leave one dark, so it applies at once, with a
    // way back for a little while.
    property var arrangementUndo: null
    function arrange(positions) {
        const map = {}, touched = []
        for (const p of positions) {
            const m = monitors.find(o => o.key === p.key)
            if (!m) continue
            const c = configFor(m)
            c.position = p.x + "x" + p.y
            map[m.key] = c
            touched.push(m)
        }
        if (touched.length === 0) return
        pending = null
        arrangementUndo = previousOf(touched)
        undoTimer.restart()
        backend.act("monitors.apply", [map], ["monitors"])
    }
    Timer { id: undoTimer; interval: 12000; onTriggered: page.arrangementUndo = null }

    function setWorkspace(id, here) {
        const change = {}
        change[String(id)] = here ? selected.key : ""
        backend.act("monitors.workspaces", [change], ["monitors"])
    }

    Connections {
        target: backend
        function onActionFinished(action, error) {
            if (action !== "monitors.apply" || !page.pending) return
            if (error.length === 0) confirm.open()
            else page.pending = null
        }
    }

    IdentifyOverlay {
        id: identify
        visible: false
        monitors: page.numbered.map(m => ({ name: m.name, number: page.numberOf(m), label: page.label(m) }))
    }

    // ---- Arrangement --------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: page.multiple ? ui.s(260) : ui.s(200)
        radius: theme.radius
        color: ui.normalFill
        border.color: ui.normalBorder

        DisplayArrangement {
            id: arrangement
            anchors.fill: parent
            anchors.margins: ui.s(12)
            anchors.bottomMargin: page.multiple ? ui.s(40) : ui.s(12)
            selectedKey: page.selected ? page.selected.key : ""
            monitors: page.arranged.map(m => {
                const size = page.logical(m)
                const mirrors = page.enabled.filter(o => o.mirrorOf && page.mirrorTarget(o) && page.mirrorTarget(o).key === m.key)
                return {
                    key: m.key, x: m.x, y: m.y, w: size.w, h: size.h,
                    label: page.label(m), number: page.numberOf(m), primary: m.primary === true,
                    detail: qsTr("%1 × %2").arg(m.width).arg(m.height) + " · "
                            + qsTr("%1 Hz").arg(Math.round(m.refreshRate).toLocaleString(Qt.locale(), 'f', 0)),
                    mirrors: mirrors.map(o => page.label(o)).join(", ")
                }
            })
            onPicked: key => page.selectedKey = key
            onArranged: positions => page.arrange(positions)
        }

        RowLayout {
            visible: page.multiple
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: ui.s(8) }
            spacing: ui.s(8)
            Text {
                Layout.fillWidth: true
                text: page.arrangementUndo ? qsTr("Displays moved.")
                      : page.arranged.length > 1 ? qsTr("Drag a display to place it beside another.") : ""
                color: ui.muted
                font.family: ui.font
                font.pixelSize: ui.caption
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
                textFormat: Text.PlainText
            }
            OmaButton {
                visible: page.arrangementUndo !== null
                text: qsTr("Undo")
                onClicked: {
                    const previous = page.arrangementUndo
                    page.arrangementUndo = null
                    undoTimer.stop()
                    backend.act("monitors.restore", [previous], ["monitors"])
                }
            }
            OmaButton {
                text: qsTr("Identify")
                glyph: "\u{F0379}"
                tooltip: qsTr("Show each display's number on the display itself")
                onClicked: identify.show()
            }
        }
        OmaButton {
            visible: !page.multiple && page.enabled.length > 0
            anchors { right: parent.right; bottom: parent.bottom; margins: ui.s(8) }
            flat: true
            glyph: "\u{F0379}"
            tooltip: qsTr("Show each display's number on the display itself")
            onClicked: identify.show()
        }
    }

    SettingsGroup {
        visible: !!page.selected && !page.selected.disabled
        title: page.selected ? (page.multiple ? qsTr("%1 · Display %2").arg(page.fullName(page.selected)).arg(page.numberOf(page.selected).toLocaleString(Qt.locale(), 'f', 0))
                                              : page.fullName(page.selected)) : ""
        footer: page.applying ? qsTr("Applying…")
              : qsTr("Changes are undone after 15 seconds unless you keep them, in case the display goes dark.")

        SettingRow {
            visible: page.multiple && !page.selected.mirrorOf
            label: qsTr("Main display")
            sublabel: qsTr("The pointer starts here, and workspace 1 opens here")
            OmaToggle {
                checked: !!page.selected && page.selected.primary === true
                onToggled: backend.act("monitors.primary", [checked ? "" : page.selected.key], ["monitors"])
                Accessible.name: qsTr("Main display")
            }
        }
        SettingRow {
            visible: page.multiple
            label: qsTr("Mirror")
            sublabel: qsTr("Show the same picture as another display")
            ChoiceBox {
                id: mirrorBox
                maxWidth: 220
                readonly property var target: page.selected ? page.mirrorTarget(page.selected) : undefined
                model: [{ value: "", text: qsTr("Don't mirror") }].concat(
                           page.enabled.filter(o => page.selected && o.key !== page.selected.key && !o.mirrorOf)
                               .map(o => ({ value: o.key, text: page.label(o) })))
                textRole: "text"; valueRole: "value"
                enabled: !page.applying
                currentIndex: Math.max(0, model.findIndex(o => o.value === (page.selected && page.selected.mirrorOf && target ? target.key : "")))
                onActivated: {
                    if (currentValue.length > 0) {
                        page.apply({ mirror: currentValue })
                        return
                    }
                    // Back to a picture of its own: beside the others.
                    let right = 0
                    for (const m of page.arranged)
                        if (m.key !== page.selected.key) right = Math.max(right, m.x + page.logical(m).w)
                    page.apply({ mirror: null, position: right + "x0" })
                }
                Accessible.name: qsTr("Mirror")
            }
        }
        SettingRow {
            label: qsTr("Resolution")
            busy: page.applying
            ChoiceBox {
                maxWidth: 220
                model: page.resolutions
                textRole: "text"; valueRole: "value"
                enabled: !page.applying
                currentIndex: page.resolutions.findIndex(r => r.value === page.resolution)
                onActivated: {
                    // Keep the refresh rate if the new resolution has it.
                    const modes = page.selected.modes.filter(m => m.split("@")[0] === currentValue)
                    const same = modes.find(m => Math.abs(Number(m.split("@")[1]) - page.selected.refreshRate) < 0.5)
                    page.apply({ mode: same || modes[0] })
                }
                Accessible.name: qsTr("Resolution")
            }
        }
        SettingRow {
            label: qsTr("Refresh rate")
            ChoiceBox {
                maxWidth: 220
                model: page.rates
                textRole: "text"; valueRole: "value"
                enabled: !page.applying
                currentIndex: page.selected ? page.rates.findIndex(r => Math.abs(Number(r.value) - page.selected.refreshRate) < 0.01) : -1
                onActivated: page.apply({ mode: page.resolution + "@" + currentValue })
                Accessible.name: qsTr("Refresh rate")
            }
        }
        SettingRow {
            label: qsTr("Scale")
            sublabel: qsTr("Larger makes text and windows bigger")
            ChoiceBox {
                maxWidth: 220
                model: page.scales
                textRole: "text"; valueRole: "value"
                enabled: !page.applying
                currentIndex: page.selected ? page.scales.findIndex(s => s.value === page.nearest(page.scales, page.selected.scale)) : -1
                onActivated: page.apply({ scale: currentValue })
                Accessible.name: qsTr("Scale")
            }
        }
        SettingRow {
            label: qsTr("Rotation")
            ChoiceBox {
                maxWidth: 220
                model: [
                    { value: 0, text: qsTr("Standard") }, { value: 1, text: qsTr("90°") },
                    { value: 2, text: qsTr("180°") }, { value: 3, text: qsTr("270°") }
                ]
                textRole: "text"; valueRole: "value"
                enabled: !page.applying
                currentIndex: page.selected ? Math.max(0, Math.min(3, page.selected.transform)) : -1
                onActivated: page.apply({ transform: currentValue })
                Accessible.name: qsTr("Rotation")
            }
        }
        ButtonRow {
            visible: page.multiple
            label: qsTr("Turn off this display")
            text: qsTr("Turn Off")
            onClicked: page.apply({ disabled: true })
        }
        ButtonRow {
            visible: !!page.selected && page.selected.managed === true
            label: qsTr("Use the settings from monitors.lua")
            sublabel: qsTr("Forgets what you chose here for this display")
            text: qsTr("Reset")
            onClicked: backend.act("monitor.reset", [page.selected.key], ["monitors"])
        }
    }

    SettingsGroup {
        visible: page.multiple && !!page.selected && !page.selected.disabled && !page.selected.mirrorOf
        title: qsTr("Workspaces")
        footer: qsTr("Chosen workspaces always open on %1; the others open on whichever display you're using.").arg(page.label(page.selected))
        SettingRow {
            label: qsTr("Open on this display")
            Flow {
                Layout.maximumWidth: ui.s(300)
                spacing: ui.s(4)
                Repeater {
                    model: 10
                    delegate: OmaButton {
                        required property int index
                        readonly property int workspace: index + 1
                        readonly property var owner: page.monitors.find(m => (m.workspaces || []).indexOf(workspace) >= 0)
                        readonly property bool here: !!owner && !!page.selected && owner.key === page.selected.key
                        width: ui.s(28)
                        text: workspace.toLocaleString(Qt.locale(), 'f', 0)
                        selected: here
                        highlighted: here
                        tooltip: here ? qsTr("Workspace %1 opens here").arg(text)
                               : owner ? qsTr("Workspace %1 opens on %2").arg(text).arg(page.label(owner))
                               : qsTr("Workspace %1 opens wherever you are").arg(text)
                        enabled: backend.busy.indexOf("monitors.workspaces") < 0
                        onClicked: page.setWorkspace(workspace, !here)
                    }
                }
            }
        }
    }

    SettingsGroup {
        id: advanced
        visible: !!page.selected && !page.selected.disabled
        property bool open: false
        title: qsTr("Advanced")
        summary: qsTr("Adaptive sync, color depth, HDR")
        collapsible: true
        collapsed: !open
        onToggle: open = !open
        footer: advanced.open ? qsTr("Not every display supports these; if the picture goes wrong, it comes back in 15 seconds.") : ""
        readonly property var saved: page.selected ? (page.selected.saved || {}) : ({})
        readonly property string cm: saved.cm || (page.selected ? page.selected.cm : "")

        SettingRow {
            visible: advanced.open
            label: qsTr("Adaptive sync")
            sublabel: qsTr("Matches the refresh rate to games and video to stop tearing")
            ChoiceBox {
                maxWidth: 220
                model: [
                    { value: 0, text: qsTr("Off") }, { value: 1, text: qsTr("On") },
                    { value: 2, text: qsTr("Full screen only") }
                ]
                textRole: "text"; valueRole: "value"
                enabled: !page.applying
                currentIndex: page.selected ? Math.max(0, Math.min(2, page.selected.vrr)) : -1
                onActivated: page.apply({ vrr: currentValue })
                Accessible.name: qsTr("Adaptive sync")
            }
        }
        SettingRow {
            visible: advanced.open
            label: qsTr("10-bit color")
            sublabel: qsTr("Smoother gradients, on displays and cables that carry it")
            OmaToggle {
                checked: advanced.saved.bitdepth === 10 || (!!page.selected && page.selected.tenBit === true)
                interactive: !page.applying
                onToggled: page.apply({ bitdepth: checked ? null : 10 })
                Accessible.name: qsTr("10-bit color")
            }
        }
        SettingRow {
            visible: advanced.open
            label: qsTr("Color mode")
            ChoiceBox {
                maxWidth: 220
                model: [
                    { value: "auto", text: qsTr("Automatic") }, { value: "srgb", text: qsTr("Standard (sRGB)") },
                    { value: "wide", text: qsTr("Wide color") }, { value: "hdr", text: qsTr("HDR") }
                ]
                textRole: "text"; valueRole: "value"
                enabled: !page.applying
                currentIndex: model.findIndex(o => o.value === (advanced.cm === "hdredid" ? "hdr" : advanced.cm))
                onActivated: page.apply(currentValue === "hdr" ? { cm: "hdr", bitdepth: 10 } : { cm: currentValue })
                Accessible.name: qsTr("Color mode")
            }
        }
        Repeater {
            model: [
                { key: "sdrbrightness", live: "sdrBrightness", label: qsTr("Brightness of regular content"), from: 0.5, to: 2 },
                { key: "sdrsaturation", live: "sdrSaturation", label: qsTr("Saturation of regular content"), from: 0.5, to: 1.5 }
            ]
            delegate: SettingRow {
                required property var modelData
                visible: advanced.open && advanced.cm.startsWith("hdr")
                label: modelData.label
                RowLayout {
                    spacing: ui.s(8)
                    OmaSlider {
                        id: sdr
                        Layout.preferredWidth: ui.s(190)
                        from: modelData.from; to: modelData.to; stepSize: 0.05
                        snapMode: OmaSlider.SnapAlways
                        enabled: !page.applying
                        value: advanced.saved[modelData.key] !== undefined ? advanced.saved[modelData.key]
                             : page.selected ? page.selected[modelData.live] : 1
                        onPressedChanged: {
                            if (pressed) return
                            const change = {}
                            change[modelData.key] = Math.round(value * 100) / 100
                            page.apply(change)
                        }
                        Accessible.name: modelData.label
                    }
                    Text {
                        text: qsTr("%1%").arg(Math.round(sdr.value * 100).toLocaleString(Qt.locale(), 'f', 0))
                        color: ui.fg
                        font.family: ui.font
                        font.pixelSize: ui.small
                        Layout.preferredWidth: ui.s(48)
                        horizontalAlignment: Text.AlignRight
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }

    SettingsGroup {
        visible: page.monitors.some(m => m.disabled)
        title: qsTr("Turned Off")
        Repeater {
            model: page.monitors.filter(m => m.disabled)
            delegate: ButtonRow {
                required property var modelData
                label: modelData.description || modelData.name
                text: qsTr("Turn On")
                onClicked: backend.act("monitor.reset", [modelData.key], ["monitors"])
            }
        }
    }

    SettingsGroup {
        SliderRow {
            visible: win.caps.backlight === true
            setting: "display.brightness"
            label: qsTr("Brightness")
            from: 1; to: 100
            live: true
            format: v => qsTr("%1%").arg(Math.round(v).toLocaleString(Qt.locale(), 'f', 0))
        }
        ToggleRow {
            setting: "display.nightLight"
            label: qsTr("Night Shift")
            sublabel: qsTr("Warmer colors that are easier on the eyes at night")
            glyph: "\u{F0594}"
        }
    }

    SettingsGroup {
        visible: (backend.lists.hardware || {}).laptop === true
        title: qsTr("Laptop Screen")
        footer: qsTr("With another display connected, the laptop's own screen can turn off or show the same picture.")
        ToggleRow { setting: "display.laptopScreen"; label: qsTr("Use the laptop's screen") }
        ToggleRow { setting: "display.mirror"; label: qsTr("Mirror the laptop's screen") }
    }

    SettingsGroup {
        visible: (backend.lists.hardware || {}).hybridGpu === true
        title: qsTr("Graphics")
        footer: qsTr("Hybrid uses the dedicated graphics card when an app needs it; integrated saves power. Switching opens a terminal and needs a restart.")
        ButtonRow {
            label: qsTr("Graphics mode")
            sublabel: (backend.lists.hardware || {}).hybridMode ? qsTr("Hybrid") : qsTr("Integrated only")
            glyph: "\u{F08AE}"
            text: (backend.lists.hardware || {}).hybridMode ? qsTr("Use Integrated Only…") : qsTr("Use Hybrid…")
            onClicked: backend.act("launch", ["hybridGpu"], ["hardware"])
        }
    }

    SettingsGroup {
        title: qsTr("Night Shift Schedule")
        footer: backend.values["display.nightSchedule"] === true
                ? qsTr("Colors warm up at the start time and go back to normal at the end time, every day.")
                : qsTr("Turn this on to warm the colors every evening without thinking about it.")
        ToggleRow { setting: "display.nightSchedule"; label: qsTr("Turn on by schedule") }
        ChoiceRow {
            visible: backend.values["display.nightSchedule"] === true
            setting: "display.nightStart"
            label: qsTr("From")
            options: (backend.meta("display.nightStart").choices || []).map(t => ({ value: t, text: t }))
        }
        ChoiceRow {
            visible: backend.values["display.nightSchedule"] === true
            setting: "display.nightEnd"
            label: qsTr("To")
            options: (backend.meta("display.nightEnd").choices || []).map(t => ({ value: t, text: t }))
        }
        SliderRow {
            visible: backend.values["display.nightSchedule"] === true
            setting: "display.nightTemperature"
            label: qsTr("Warmth")
            from: 6000; to: 2500; stepSize: 100
            fromText: qsTr("Less warm"); toText: qsTr("More warm")
            format: v => qsTr("%1 K").arg(Math.round(v).toLocaleString(Qt.locale(), 'f', 0))
        }
    }

    OmaDialog {
        id: confirm
        title: qsTr("Keep these display settings?")
        acceptText: qsTr("Keep Changes")
        rejectText: qsTr("Revert")
        closePolicy: Popup.NoAutoClose
        property int remaining: 15
        onOpened: { remaining = 15; countdown.start() }
        onClosed: countdown.stop()
        onAccepted: page.pending = null
        onRejected: confirm.revert()
        Timer {
            id: countdown
            interval: 1000
            repeat: true
            onTriggered: {
                confirm.remaining--
                if (confirm.remaining <= 0) { confirm.close(); confirm.revert() }
            }
        }
        function revert() {
            if (page.pending)
                backend.act("monitors.restore", [page.pending.previous], ["monitors"])
            page.pending = null
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Going back to the previous settings in %n seconds.", "", confirm.remaining)
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
