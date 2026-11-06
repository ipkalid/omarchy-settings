import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    pane: "displays"
    title: qsTr("Displays")
    tagline: page.monitors.length > 0 ? qsTr("%n displays", "", page.monitors.filter(m => !m.disabled).length) : ""
    glyph: "\u{F0379}"
    lists: ["monitors"]

    readonly property var monitors: backend.lists.monitors || []
    property string selectedName: ""
    readonly property var selected: monitors.find(m => m.name === selectedName) || monitors.find(m => m.focused) || monitors[0]
    readonly property bool applying: backend.busy.indexOf("monitor.apply") >= 0 || backend.busy.indexOf("monitor.restore") >= 0

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
    readonly property string rate: selected ? Number(selected.refreshRate).toFixed(2) : ""
    readonly property var scales: [1, 1.25, 1.333333, 1.5, 1.6, 1.666667, 1.75, 2, 2.5, 3].map(s => ({
        value: s, text: qsTr("%1%").arg(Math.round(s * 100).toLocaleString(Qt.locale(), 'f', 0)) }))

    // Changing a display can leave it showing nothing, so like macOS the
    // change is undone unless it is confirmed in time.
    property var pending: null
    function apply(changes) {
        const m = selected
        const config = {
            mode: changes.mode || (m.width + "x" + m.height + "@" + Number(m.refreshRate).toFixed(2)),
            position: m.x + "x" + m.y,
            scale: changes.scale !== undefined ? changes.scale : m.scale,
            transform: changes.transform !== undefined ? changes.transform : m.transform
        }
        if (changes.disabled) config.disabled = true
        pending = { name: m.name, previous: backend.savedMonitor(m.name) }
        backend.act("monitor.apply", [m.name, config], ["monitors"])
    }
    function nearest(list, value) {
        let best = null
        for (const o of list)
            if (best === null || Math.abs(o.value - value) < Math.abs(best.value - value)) best = o
        return best ? best.value : value
    }

    Connections {
        target: backend
        function onActionFinished(action, error) {
            if (action !== "monitor.apply" || !page.pending) return
            if (error.length === 0) confirm.open()
            else page.pending = null
        }
    }

    // ---- Arrangement --------------------------------------------------------
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 200
        radius: theme.radius
        color: ui.normalFill
        border.color: ui.normalBorder

        Item {
            id: arrangement
            anchors.fill: parent
            anchors.margins: 24
            readonly property var bounds: {
                let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9
                for (const m of page.monitors) {
                    if (m.disabled) continue
                    const w = (m.transform % 2 ? m.height : m.width) / m.scale, h = (m.transform % 2 ? m.width : m.height) / m.scale
                    x0 = Math.min(x0, m.x); y0 = Math.min(y0, m.y); x1 = Math.max(x1, m.x + w); y1 = Math.max(y1, m.y + h)
                }
                return x1 > x0 ? { x: x0, y: y0, w: x1 - x0, h: y1 - y0 } : { x: 0, y: 0, w: 1, h: 1 }
            }
            readonly property real factor: Math.min(width / bounds.w, height / bounds.h) * 0.9

            Repeater {
                model: page.monitors.filter(m => !m.disabled)
                delegate: Rectangle {
                    required property var modelData
                    readonly property bool chosen: page.selected && modelData.name === page.selected.name
                    readonly property real w: (modelData.transform % 2 ? modelData.height : modelData.width) / modelData.scale
                    readonly property real h: (modelData.transform % 2 ? modelData.width : modelData.height) / modelData.scale
                    x: (arrangement.width - arrangement.bounds.w * arrangement.factor) / 2 + (modelData.x - arrangement.bounds.x) * arrangement.factor
                    y: (arrangement.height - arrangement.bounds.h * arrangement.factor) / 2 + (modelData.y - arrangement.bounds.y) * arrangement.factor
                    width: w * arrangement.factor - 4
                    height: h * arrangement.factor - 4
                    radius: Math.min(4, theme.radius)
                    color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, chosen ? 0.35 : 0.14)
                    border.color: chosen ? ui.accent : ui.normalBorder
                    border.width: chosen ? 2 : 1
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 12
                        horizontalAlignment: Text.AlignHCenter
                        text: parent.modelData.model || parent.modelData.name
                        color: ui.fg
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    TapHandler { onTapped: page.selectedName = parent.modelData.name }
                }
            }
        }
    }

    SettingsGroup {
        visible: !!page.selected
        title: page.selected ? (page.selected.make ? page.selected.make + " " + page.selected.model : page.selected.name) : ""
        footer: page.applying ? qsTr("Applying…")
              : qsTr("Changes are undone after 15 seconds unless you keep them, in case the display goes dark.")

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
            visible: page.monitors.filter(m => !m.disabled).length > 1
            label: qsTr("Turn off this display")
            text: qsTr("Turn Off")
            onClicked: page.apply({ disabled: true })
        }
        ButtonRow {
            visible: !!page.selected && page.selected.managed === true
            label: qsTr("Use the settings from monitors.lua")
            sublabel: qsTr("Forgets what you chose here for this display")
            text: qsTr("Reset")
            onClicked: backend.act("monitor.reset", [page.selected.name], ["monitors"])
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
                onClicked: backend.act("monitor.reset", [modelData.name], ["monitors"])
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
                backend.act("monitor.restore", [page.pending.name, page.pending.previous], ["monitors"])
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
