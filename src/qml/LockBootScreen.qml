import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The screen that asks for the disk password when the computer starts
// (Plymouth). It can take Omarchy's look or any theme's.
SettingsPage {
    id: page
    pane: ""
    lists: ["bootScreens"]

    readonly property var screens: backend.lists.bootScreens || []

    SettingsGroup {
        separated: false
        footer: qsTr("Changing it rebuilds the boot image in a terminal and asks for your password. You'll see it the next time the computer starts.")
        Item {
            width: parent.width
            implicitHeight: grid.implicitHeight + ui.s(24)
            GridLayout {
                id: grid
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: ui.s(12)
                columns: Math.max(2, Math.floor(width / Math.round(150 * ui.scale)))
                columnSpacing: ui.s(10)
                rowSpacing: ui.s(12)
                Repeater {
                    model: page.screens
                    delegate: ColumnLayout {
                        id: tile
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: ui.s(6)
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: width * 0.6
                            radius: theme.radius
                            color: ui.bg
                            border.width: tile.modelData.current ? 3 : (hover.hovered ? 2 : 1)
                            border.color: tile.modelData.current ? ui.accent : hover.hovered ? ui.muted : ui.normalBorder
                            clip: true
                            Image {
                                anchors.fill: parent
                                anchors.margins: parent.border.width
                                source: tile.modelData.preview
                                fillMode: Image.PreserveAspectCrop
                                sourceSize.width: 320
                                asynchronous: true
                            }
                            HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                onTapped: if (!tile.modelData.current) { confirm.target = tile.modelData.name; confirm.open() }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: tile.modelData.name === "default" ? qsTr("Omarchy") : tile.modelData.name.replace(/-/g, " ")
                            color: tile.modelData.current ? ui.fg : ui.muted
                            font.family: ui.font
                            font.pixelSize: ui.small
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                        }
                    }
                }
            }
        }
    }

    OmaDialog {
        id: confirm
        property string target
        title: qsTr("Use the “%1” boot screen?").arg(target === "default" ? qsTr("Omarchy") : target)
        acceptText: qsTr("Use It")
        onAccepted: backend.act("launch", [target === "default" ? "plymouthReset" : "bootScreen:" + target], ["bootScreens"])
        Text {
            Layout.fillWidth: true
            text: qsTr("A terminal opens to rebuild the boot image; it takes a minute and asks for your password.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
