import QtQuick
import QtQuick.Layouts

// A section of a pane, as the shell's panels lay them out: a thin separator
// above, an uppercase caption, its rows, and an optional note.
ColumnLayout {
    id: group
    property string title
    property string footer
    property bool separated: true
    default property alias rows: list.data

    Layout.fillWidth: true
    spacing: ui.s(6)

    Rectangle {
        visible: group.separated
        Layout.fillWidth: true
        Layout.bottomMargin: ui.s(6)
        implicitHeight: 1
        color: ui.separator
    }

    Text {
        visible: group.title.length > 0
        text: group.title.toLocaleUpperCase(Qt.locale())
        horizontalAlignment: Text.AlignLeft
        color: ui.muted
        font.family: ui.font
        font.pixelSize: ui.caption
        font.bold: true
        // Spacing letters apart breaks up joined scripts like Arabic.
        font.letterSpacing: Qt.application.layoutDirection === Qt.RightToLeft ? 0 : 0.5
        Layout.fillWidth: true
        textFormat: Text.PlainText
    }

    Column {
        id: list
        Layout.fillWidth: true
        spacing: ui.s(2)
    }

    Text {
        visible: group.footer.length > 0
        text: group.footer
        horizontalAlignment: Text.AlignLeft
        color: ui.muted2
        font.family: ui.font
        font.pixelSize: ui.caption
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        textFormat: Text.PlainText
    }
}
