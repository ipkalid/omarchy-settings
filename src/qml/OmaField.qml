import QtQuick
import QtQuick.Controls

// A text input in the shell's style: an outlined box in the monospace font.
TextField {
    id: field
    implicitHeight: ui.controlHeight + ui.s(4)
    leftPadding: ui.s(10)
    rightPadding: ui.s(10)
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    horizontalAlignment: TextInput.AlignLeft
    font.family: ui.font
    font.pixelSize: ui.body
    color: ui.fg
    placeholderTextColor: ui.muted
    selectionColor: ui.accent
    selectedTextColor: theme.accentForeground
    selectByMouse: true
    background: Rectangle {
        radius: theme.radius
        color: field.activeFocus ? ui.hoverFill : ui.normalFill
        border.width: 1
        border.color: field.activeFocus ? ui.accent : field.hovered ? ui.alpha(ui.fg, 0.55) : ui.normalBorder
    }
}
