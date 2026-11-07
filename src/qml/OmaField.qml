import QtQuick
import QtQuick.Controls.Basic as Basic

// A text input in the shell's style: an outlined box in the monospace font.
// Built on the Basic style's field, whose placeholder stays inside the box;
// Material's floats up onto the border as a label when the field takes
// focus, which the shell's fields never do.
Basic.TextField {
    id: field
    // The same height as buttons and choice boxes, so a field lines up
    // with the controls beside it.
    implicitHeight: ui.controlHeight
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
