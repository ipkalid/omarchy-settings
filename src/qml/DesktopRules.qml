import QtQuick
import QtQuick.Layouts

// Rules for how particular apps' windows open, written through Omarchy's
// o.window helper into the generated Hyprland file.
SettingsPage {
    id: page
    pane: ""
    lists: ["windowRules"]

    readonly property var info: backend.lists.windowRules || ({})
    readonly property var rules: info.rules || []
    readonly property var windows: info.windows || []

    function describe(rule) {
        const parts = []
        if (rule.float) parts.push(qsTr("floating"))
        if (rule.center) parts.push(qsTr("centered"))
        if (rule.workspace) parts.push(qsTr("on workspace %1").arg(rule.workspace))
        if (rule.width && rule.height) parts.push(qsTr("%1 × %2").arg(rule.width).arg(rule.height))
        if (rule.opacity) parts.push(qsTr("%1% opaque").arg(Math.round(rule.opacity * 100)))
        if (rule.pin) parts.push(qsTr("on every workspace"))
        if (rule.fullscreen) parts.push(qsTr("full screen"))
        if (rule.tile) parts.push(qsTr("tiled"))
        return parts.join(", ")
    }

    SettingsGroup {
        separated: false
        title: qsTr("Rules")
        footer: page.rules.length === 0 ? qsTr("No rules yet. Rules apply to windows as they open.")
                                        : qsTr("Rules apply to windows as they open; close and reopen an app to see a new rule.")
        Repeater {
            model: page.rules
            delegate: SettingRow {
                required property var modelData
                required property int index
                label: modelData.class || modelData.title
                sublabel: page.describe(modelData)
                glyph: "\u{F05B2}"
                OmaButton {
                    glyph: "\u{F01B4}"
                    bordered: false
                    tooltip: qsTr("Delete")
                    onClicked: backend.act("rule.remove", [index], ["windowRules"])
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("New Rule")
        footer: qsTr("Pick an open window's app, or type its class. Find any app's class with: hyprctl clients")

        SettingRow {
            label: qsTr("App")
            ChoiceBox {
                id: openApps
                maxWidth: Math.round(240 * ui.scale)
                model: page.windows
                textRole: "class"
                valueRole: "class"
                currentIndex: -1
                placeholder: qsTr("From an open window")
                onCountChanged: Qt.callLater(() => currentIndex = -1)
                onActivated: appClass.text = currentValue
                Accessible.name: qsTr("Open window")
            }
            OmaField {
                id: appClass
                width: Math.round(200 * ui.scale)
                placeholderText: qsTr("App class")
            }
        }
        SettingRow {
            label: qsTr("Open it floating")
            OmaToggle { id: floating; onToggled: checked = !checked }
        }
        SettingRow {
            label: qsTr("Center it")
            OmaToggle { id: centered; onToggled: checked = !checked }
        }
        SettingRow {
            label: qsTr("On workspace")
            ChoiceBox {
                id: workspace
                maxWidth: Math.round(160 * ui.scale)
                model: [qsTr("Any")].concat([1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map(n => String(n)))
                currentIndex: 0
                Accessible.name: qsTr("Workspace")
            }
        }
        SettingRow {
            label: qsTr("Size")
            sublabel: qsTr("For floating windows; leave empty to let the app decide")
            OmaField { id: width; width: Math.round(90 * ui.scale); placeholderText: qsTr("Width"); validator: IntValidator { bottom: 100; top: 10000 } }
            OmaField { id: height; width: Math.round(90 * ui.scale); placeholderText: qsTr("Height"); validator: IntValidator { bottom: 100; top: 10000 } }
        }
        SettingRow {
            label: qsTr("Opacity")
            OmaSlider {
                id: opacity
                width: Math.round(200 * ui.scale)
                from: 0.3; to: 1; stepSize: 0.05; value: 1
            }
            Text {
                text: qsTr("%1%").arg(Math.round(opacity.value * 100))
                color: ui.fg
                font.family: ui.font
                font.pixelSize: ui.small
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
            }
        }
        SettingRow {
            label: qsTr("Show on every workspace")
            OmaToggle { id: pinned; onToggled: checked = !checked }
        }
        SettingRow {
            label: qsTr("Open it full screen")
            OmaToggle { id: fullscreen; onToggled: checked = !checked }
        }
        ButtonRow {
            label: ""
            text: qsTr("Add Rule")
            highlighted: true
            enabled: appClass.text.trim().length > 0 && (floating.checked || centered.checked || workspace.currentIndex > 0
                     || (width.text && height.text) || opacity.value < 1 || pinned.checked || fullscreen.checked)
            onClicked: {
                const rule = { class: "^(" + appClass.text.trim().replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + ")$" }
                if (floating.checked) rule.float = true
                if (centered.checked) rule.center = true
                if (workspace.currentIndex > 0) rule.workspace = workspace.currentText
                if (width.text && height.text) { rule.width = Number(width.text); rule.height = Number(height.text) }
                if (opacity.value < 1) rule.opacity = opacity.value
                if (pinned.checked) rule.pin = true
                if (fullscreen.checked) rule.fullscreen = true
                backend.act("rule.add", [rule], ["windowRules"])
                appClass.text = ""
                floating.checked = centered.checked = pinned.checked = fullscreen.checked = false
                workspace.currentIndex = 0
                width.text = height.text = ""
                opacity.value = 1
            }
        }
    }
}
