import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    pane: "appearance"
    title: qsTr("Appearance")
    tagline: page.current
    glyph: "\u{F03D8}"
    description: qsTr("Choose a theme for the whole desktop, and how windows and text look.")
    lists: ["themes", "fonts", "fontCatalog"]

    readonly property var themes: backend.lists.themes || []
    readonly property string current: backend.values["appearance.theme"] || ""
    readonly property bool switching: backend.busy.indexOf("appearance.theme") >= 0

    SettingsGroup {
        title: qsTr("Theme")
        footer: page.switching ? qsTr("Applying “%1” to the desktop…").arg(page.current)
                               : qsTr("A theme sets the colors, wallpaper, and fonts of everything at once.")
        // The current theme, opening the gallery of all of them on a page of
        // its own.
        SettingRow {
            id: themeRow
            label: page.current
            sublabel: qsTr("%n themes installed", "", page.themes.length)
            clickable: true
            busy: page.switching
            activeFocusOnTab: true
            onActivated: win.openSub("AppearanceThemes", qsTr("Themes"))
            Keys.onReturnPressed: activated()
            Keys.onSpacePressed: activated()
            implicitHeight: Math.round(90 * ui.scale)
            Rectangle {
                width: Math.round(120 * ui.scale)
                height: Math.round(72 * ui.scale)
                radius: theme.radius
                color: ui.bg
                border.width: 1
                border.color: ui.normalBorder
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: (page.themes.find(t => t.current) || {}).preview || ""
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: 240
                    asynchronous: true
                }
            }
            OmaButton {
                text: qsTr("Browse Themes")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: themeRow.activated()
            }
        }
    }

    SettingsGroup {
        title: qsTr("Windows")
        ChoiceRow {
            setting: "appearance.corners"
            label: qsTr("Window corners")
            options: [
                { value: "theme", text: qsTr("As the theme sets them") },
                { value: "sharp", text: qsTr("Sharp") },
                { value: "rounded", text: qsTr("Rounded") }
            ]
        }
        ChoiceRow {
            setting: "appearance.transparency"
            label: qsTr("Window transparency")
            options: [
                { value: "opaque", text: qsTr("Opaque") },
                { value: "transparent", text: qsTr("Transparent") },
                { value: "glassy", text: qsTr("Glassy") }
            ]
        }
        ToggleRow {
            setting: "appearance.animations"
            label: qsTr("Animations, blur, and shadows")
            sublabel: qsTr("Turn off on computers without a graphics card to keep things quick")
        }
    }

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
