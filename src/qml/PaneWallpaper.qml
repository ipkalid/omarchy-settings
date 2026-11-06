import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    pane: "wallpaper"
    title: qsTr("Wallpaper")
    tagline: page.current ? page.current.name : ""
    glyph: "\u{F02E9}"
    lists: ["backgrounds"]

    readonly property var backgrounds: backend.lists.backgrounds || []
    readonly property var current: backgrounds.find(b => b.current)
    readonly property bool setting: backend.busy.indexOf("background.set") >= 0 || backend.busy.indexOf("background.next") >= 0

    function choose(b) { backend.act("background.set", [b.path], ["backgrounds"]) }

    // The current wallpaper, large, with the way to the next one; in a
    // narrow window the buttons go under it.
    GridLayout {
        id: hero
        Layout.fillWidth: true
        readonly property bool narrow: width < Math.round(520 * ui.scale)
        columns: narrow ? 1 : 2
        columnSpacing: ui.s(18)
        rowSpacing: ui.s(12)
        Rectangle {
            Layout.fillWidth: hero.narrow
            Layout.preferredWidth: Math.round(260 * ui.scale)
            Layout.preferredHeight: Math.round(width * 0.615)
            radius: theme.radius
            color: ui.normalFill
            border.color: ui.normalBorder
            clip: true
            Image {
                anchors.fill: parent
                source: page.current && !page.current.video ? page.current.url : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 520
                asynchronous: true
            }
            Spinner { anchors.centerIn: parent; running: page.setting; visible: running }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: qsTr("From the %1 theme").arg(backend.values["appearance.theme"] || "")
                visible: (backend.values["appearance.theme"] || "").length > 0
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                color: ui.muted
                font.family: ui.font
                font.pixelSize: ui.small
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Flow {
                Layout.fillWidth: true
                spacing: ui.s(8)
                OmaButton {
                    text: qsTr("Next Wallpaper")
                    onClicked: backend.act("background.next", [], ["backgrounds"])
                }
                OmaButton {
                    text: qsTr("Add Pictures…")
                    onClicked: backend.act("wallpaper.add", [], ["backgrounds"])
                }
                OmaButton {
                    glyph: "\u{F024B}"
                    bordered: false
                    tooltip: qsTr("Open the folder")
                    onClicked: backend.act("wallpaper.openFolder", [])
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Wallpapers for this theme")
        footer: qsTr("Add your own pictures to %1 to see them here.").arg("~/.config/omarchy/backgrounds/")

        Item {
            width: parent.width
            implicitHeight: wallGrid.implicitHeight + 24
            GridLayout {
                id: wallGrid
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                columns: Math.max(1, Math.floor(width / Math.round(130 * ui.scale)))
                columnSpacing: 10
                rowSpacing: 10
                Repeater {
                    model: page.backgrounds
                    delegate: Rectangle {
                        id: wall
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: width * 0.62
                        radius: theme.radius
                        color: ui.bg
                        border.width: modelData.current ? 3 : (hover.hovered || activeFocus ? 2 : 1)
                        border.color: modelData.current ? ui.accent : hover.hovered || activeFocus ? ui.muted : ui.normalBorder
                        clip: true
                        activeFocusOnTab: true
                        Keys.onReturnPressed: page.choose(modelData)
                        Keys.onSpacePressed: page.choose(modelData)
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData.name

                        Image {
                            anchors.fill: parent
                            anchors.margins: parent.border.width
                            visible: !wall.modelData.video
                            source: wall.modelData.video ? "" : wall.modelData.url
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: 320
                            asynchronous: true
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: wall.modelData.video
                            text: "\u{F040A}"
                            font.family: ui.iconFont
                            font.pixelSize: 28
                            color: ui.muted
                        }
                        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: page.choose(wall.modelData) }
                        OmaToolTip { visible: hover.hovered; text: wall.modelData.name }
                    }
                }
            }
        }
    }
}
