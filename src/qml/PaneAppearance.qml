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
    lists: ["themes", "fonts"]

    readonly property var themes: backend.lists.themes || []
    readonly property string current: backend.values["appearance.theme"] || ""
    readonly property bool switching: backend.busy.indexOf("appearance.theme") >= 0

    SettingsGroup {
        title: qsTr("Theme")
        footer: page.switching ? qsTr("Applying “%1” to the desktop…").arg(page.current)
                               : qsTr("A theme sets the colors, wallpaper, and fonts of everything at once.")

        Item {
            width: parent.width
            implicitHeight: grid.implicitHeight + 24

            GridLayout {
                id: grid
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                columns: Math.max(2, Math.floor(width / 150))
                columnSpacing: 10
                rowSpacing: 12

                Repeater {
                    model: page.themes
                    delegate: ColumnLayout {
                        id: tile
                        required property var modelData
                        readonly property bool selected: modelData.name === page.current
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 6

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: width * 0.6
                            radius: theme.radius
                            color: ui.bg
                            border.width: tile.selected ? 3 : (themeHover.hovered || tile.activeFocus ? 2 : 1)
                            border.color: tile.selected ? ui.accent : themeHover.hovered || tile.activeFocus ? ui.muted : ui.normalBorder
                            clip: true

                            Image {
                                anchors.fill: parent
                                anchors.margins: parent.border.width
                                source: tile.modelData.preview
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                sourceSize.width: 320
                                smooth: true
                            }
                            Spinner {
                                anchors.centerIn: parent
                                running: tile.selected && page.switching
                                visible: running
                            }
                            HoverHandler { id: themeHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: backend.set("appearance.theme", tile.modelData.name) }
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: tile.modelData.name
                            color: tile.selected ? ui.fg : ui.muted
                            font.pixelSize: ui.small
                            font.weight: tile.selected ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                        activeFocusOnTab: true
                        Keys.onReturnPressed: backend.set("appearance.theme", modelData.name)
                        Keys.onSpacePressed: backend.set("appearance.theme", modelData.name)
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.name
                    }
                }
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
        ChoiceRow {
            setting: "appearance.barBackground"
            label: qsTr("Bar background")
            options: [
                { value: "opaque", text: qsTr("Opaque") },
                { value: "glassy", text: qsTr("Glassy") },
                { value: "none", text: qsTr("None") }
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
}
