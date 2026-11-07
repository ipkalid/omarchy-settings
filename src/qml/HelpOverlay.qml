import QtQuick
import QtQuick.Layouts

// Every keyboard shortcut, shown with ? or the ? button. A click outside the
// card, Esc, or the close button puts it away.
Rectangle {
    id: overlay
    color: Qt.rgba(0, 0, 0, 0.55)

    TapHandler { onTapped: win.helpVisible = false }

    // Alternatives for the same action are separate key caps.
    readonly property var shortcuts: [
        { keys: ["?"], action: qsTr("Show or hide these shortcuts") },
        { keys: ["Ctrl+F", "/"], action: qsTr("Search settings") },
        { keys: ["↑", "↓"], action: qsTr("Move between panes in the sidebar") },
        { keys: ["Ctrl+↑", "Ctrl+↓"], action: qsTr("Previous or next pane from anywhere") },
        { keys: ["Tab"], action: qsTr("Move between controls") },
        { keys: ["Space"], action: qsTr("Toggle the focused switch or press the focused button") },
        { keys: ["Alt+←", "Alt+→"], action: qsTr("Back and forward (or the mouse's side buttons)") },
        { keys: ["Esc"], action: qsTr("Clear the search, or go back") },
        { keys: ["Ctrl+Z"], action: qsTr("Undo the last change") },
        { keys: ["Ctrl+Shift+Z"], action: qsTr("Redo") },
        { keys: ["Ctrl+R"], action: qsTr("Reload what this pane shows") },
        { keys: ["Q"], action: qsTr("Quit") },
    ]

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - ui.s(48), ui.s(600))
        height: Math.min(parent.height - ui.s(48), content.implicitHeight + ui.s(44))
        // Solid, so the page behind never shows through the text.
        color: ui.bg
        radius: theme.radius
        border.color: ui.normalBorder
        clip: true

        TapHandler {}

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: ui.s(22)
            spacing: ui.s(14)

            RowLayout {
                Layout.fillWidth: true
                spacing: ui.s(8)
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Keyboard Shortcuts")
                    horizontalAlignment: Text.AlignLeft
                    color: ui.fg
                    font.family: ui.font
                    font.pixelSize: ui.title
                    font.bold: true
                    textFormat: Text.PlainText
                }
                OmaButton {
                    glyph: "\u{F0156}"
                    bordered: false
                    tooltip: qsTr("Dismiss")
                    onClicked: win.helpVisible = false
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: ui.separator }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: ui.s(20)
                rowSpacing: ui.s(10)

                Repeater {
                    model: overlay.shortcuts
                    delegate: Row {
                        required property var modelData
                        required property int index
                        Layout.row: index
                        Layout.column: 0
                        Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                        spacing: ui.s(6)
                        Repeater {
                            model: modelData.keys
                            delegate: Rectangle {
                                required property string modelData
                                implicitWidth: Math.max(implicitHeight, cap.implicitWidth + ui.s(14))
                                implicitHeight: cap.implicitHeight + ui.s(6)
                                radius: Math.min(ui.s(4), theme.radius)
                                color: ui.normalFill
                                border.color: ui.normalBorder
                                Text {
                                    id: cap
                                    anchors.centerIn: parent
                                    text: parent.modelData
                                    color: ui.accent
                                    font.family: ui.font
                                    font.pixelSize: ui.small
                                    textFormat: Text.PlainText
                                }
                            }
                        }
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
                        Layout.alignment: Qt.AlignTop
                        topPadding: ui.s(3)
                        text: modelData.action
                        horizontalAlignment: Text.AlignLeft
                        color: ui.fg
                        font.family: ui.font
                        font.pixelSize: ui.body
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }
}
