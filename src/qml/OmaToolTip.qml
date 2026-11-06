import QtQuick
import QtQuick.Controls

// The shell's tooltip: its background, foreground, and a thin border.
ToolTip {
    id: tip
    delay: 500
    font.family: ui.font
    font.pixelSize: ui.small
    contentItem: Text {
        text: tip.text
        color: ui.fg
        font: tip.font
        textFormat: Text.PlainText
    }
    background: Rectangle {
        color: ui.bg
        border.color: ui.fg
        border.width: 1
        radius: theme.radius
    }
}
