import QtQuick

// A row that opens a page of its own, with a chevron pointing the way it
// opens in the reading direction.
SettingRow {
    id: row
    property string detail
    signal open()

    clickable: true
    onActivated: row.open()
    activeFocusOnTab: true
    Keys.onReturnPressed: row.open()
    Keys.onSpacePressed: row.open()

    Rectangle {
        parent: row
        anchors.fill: parent
        visible: row.activeFocus
        color: "transparent"
        border.color: ui.accent
        border.width: 1
        radius: theme.radius
    }

    Text {
        visible: row.detail.length > 0
        text: row.detail
        color: ui.muted
        font.family: ui.font
        font.pixelSize: ui.body
        textFormat: Text.PlainText
    }
    Text {
        text: Qt.application.layoutDirection === Qt.RightToLeft ? "\u{F0141}" : "\u{F0142}"
        color: ui.muted
        font.family: ui.iconFont
        font.pixelSize: ui.body
        textFormat: Text.PlainText
    }
}
