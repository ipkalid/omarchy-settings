import QtQuick
import QtQuick.Layouts

// Fonts: the monospace font the desktop draws in, how big text is, how
// Chinese, Japanese, and Korean are drawn, and more fonts to install.
SettingsPage {
    id: page
    pane: "fonts"
    resetPane: "fonts"
    title: qsTr("Fonts")
    tagline: backend.values["appearance.font"] || ""
    glyph: "\u{F06D6}"
    description: qsTr("Choose the font the desktop, terminals, and the bar use, and how big text is.")
    lists: ["fonts", "fontCatalog"]

    SettingsGroup {
        title: qsTr("Text")
        footer: qsTr("Text size changes the bar, apps, and terminals together.")
        SliderRow {
            setting: "appearance.textSize"
            label: qsTr("Text size")
            from: 9
            to: 20
            fromText: qsTr("Smaller")
            toText: qsTr("Larger")
            format: v => qsTr("%1 px").arg(Math.round(v).toLocaleString(Qt.locale(), 'f', 0))
        }
        ChoiceRow {
            setting: "appearance.font"
            label: qsTr("Monospace font")
            sublabel: qsTr("Used by terminals and the bar")
            menuWidth: 260
            options: (backend.lists.fonts || []).map(f => ({ value: f, text: f }))
        }
    }

    SettingsGroup {
        title: qsTr("Chinese, Japanese, and Korean")
        footer: qsTr("The same characters are drawn differently in each language. Pick the one you read; apps show it after they restart.")
        ChoiceRow {
            setting: "fonts.cjk"
            label: qsTr("Draw CJK characters as")
            menuWidth: Math.round(240 * ui.scale)
            options: [
                { value: "default", text: qsTr("Automatic") }, { value: "jp", text: qsTr("Japanese") },
                { value: "kr", text: qsTr("Korean") }, { value: "sc", text: qsTr("Simplified Chinese") },
                { value: "tc", text: qsTr("Traditional Chinese") }, { value: "hk", text: qsTr("Hong Kong Chinese") }
            ]
        }
    }

    SettingsGroup {
        title: qsTr("More Fonts")
        footer: qsTr("Coding fonts Omarchy offers. Installing one opens a terminal and switches to it when done.")
        Repeater {
            model: backend.lists.fontCatalog || []
            delegate: SettingRow {
                required property var modelData
                label: modelData.name
                sublabel: modelData.installed ? qsTr("Installed") : ""
                OmaButton {
                    text: modelData.installed ? qsTr("Use") : qsTr("Install…")
                    selected: modelData.installed && backend.values["appearance.font"] === modelData.family
                    enabled: !(modelData.installed && backend.values["appearance.font"] === modelData.family)
                    onClicked: modelData.installed ? backend.set("appearance.font", modelData.family)
                                                   : backend.act("launch", ["font:" + modelData.key], ["fontCatalog", "fonts"])
                }
            }
        }
    }
}
