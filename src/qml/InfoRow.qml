import QtQuick

// A label and a value that can be selected and copied, the way the shell's
// panels list facts: the label dimmed, the value in full color.
SettingRow {
    id: row
    property string value

    TextEdit {
        text: row.value
        readOnly: true
        selectByMouse: true
        color: ui.fg
        font.family: ui.font
        font.pixelSize: ui.body
        horizontalAlignment: Text.AlignRight
        width: Math.min(implicitWidth, row.width * 0.6)
        wrapMode: TextEdit.Wrap
        textFormat: TextEdit.PlainText
        selectionColor: ui.accent
        selectedTextColor: theme.accentForeground
    }
}
