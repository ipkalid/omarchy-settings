import QtQuick
import QtQuick.Controls

// The shell's slider (qs.Ui PanelSlider): a faint track filled in the
// foreground color, with a round knob that grows under the pointer.
Slider {
    id: slider
    implicitHeight: Math.max(ui.s(22), knob.height + ui.s(6))
    padding: 0
    leftPadding: knob.width / 2
    rightPadding: knob.width / 2

    background: Item {
        x: slider.leftPadding
        y: (slider.height - height) / 2
        width: slider.availableWidth
        height: Math.max(4, Math.round(ui.controlHeight * 0.11))
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: ui.selectedFill
        }
        Rectangle {
            // visualPosition already counts from the reading start.
            x: slider.mirrored ? parent.width * slider.visualPosition : 0
            width: slider.mirrored ? parent.width * (1 - slider.visualPosition) : parent.width * slider.visualPosition
            height: parent.height
            radius: height / 2
            color: ui.fg
        }
    }

    handle: Rectangle {
        id: knob
        width: Math.max(Math.round(14 * ui.scale), Math.round(ui.controlHeight * 0.38))
        height: width
        radius: width / 2
        x: slider.leftPadding + slider.visualPosition * slider.availableWidth - width / 2
        y: (slider.height - height) / 2
        color: ui.fg
        border.color: ui.bg
        border.width: Math.max(1, ui.s(2))
        scale: slider.hovered || slider.pressed || slider.activeFocus ? 1.15 : 1
        Behavior on scale { NumberAnimation { duration: 110; easing.type: Easing.OutCubic } }
    }
}
