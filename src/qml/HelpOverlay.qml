import QtQuick
import QtQuick.Layouts

// Every keyboard shortcut, shown with ?.
Rectangle {
    id: overlay
    color: Qt.rgba(0, 0, 0, 0.55)

    TapHandler { onTapped: win.helpVisible = false }

    readonly property var shortcuts: [
        { keys: "?", action: qsTr("Show or hide these shortcuts") },
        { keys: "Ctrl+F  /", action: qsTr("Search settings") },
        { keys: "↑  ↓", action: qsTr("Move between panes in the sidebar") },
        { keys: "Ctrl+↑  Ctrl+↓", action: qsTr("Previous or next pane from anywhere") },
        { keys: "Tab", action: qsTr("Move between controls") },
        { keys: "Space", action: qsTr("Toggle the focused switch or press the focused button") },
        { keys: "Alt+←  Alt+→", action: qsTr("Back and forward (or the mouse's side buttons)") },
        { keys: "Esc", action: qsTr("Clear the search, or go back") },
        { keys: "Ctrl+Z", action: qsTr("Undo the last change") },
        { keys: "Ctrl+Shift+Z", action: qsTr("Redo") },
        { keys: "Ctrl+R", action: qsTr("Reload what this pane shows") },
        { keys: "Q", action: qsTr("Quit") },
    ]

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 520)
        height: content.implicitHeight + 40
        color: ui.normalFill
        radius: theme.radius
        border.color: ui.normalBorder

        TapHandler {}

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Text {
                text: qsTr("Keyboard Shortcuts")
                color: ui.fg
                font.pixelSize: Math.round(ui.body * 1.2)
                font.weight: Font.DemiBold
                textFormat: Text.PlainText
            }
            GridLayout {
                columns: 2
                columnSpacing: 20
                rowSpacing: 8
                Layout.fillWidth: true
                Repeater {
                    model: overlay.shortcuts
                    delegate: Text {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        text: modelData.keys
                        color: ui.accent
                        font.family: ui.iconFont
                        font.pixelSize: ui.small
                        textFormat: Text.PlainText
                    }
                }
                Repeater {
                    model: overlay.shortcuts
                    delegate: Text {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 1
                        Layout.fillWidth: true
                        text: modelData.action
                        color: ui.fg
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }
}
