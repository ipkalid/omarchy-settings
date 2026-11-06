import QtQuick

// The Omarchy shell's toggle (qs.Ui ToggleSwitch): a box with a knob,
// square unless windows are rounded, filled when on.
Item {
    id: root
    property bool checked: false
    property bool interactive: true
    readonly property bool rounded: theme.radius > 0
    readonly property bool mirrored: LayoutMirroring.enabled
    readonly property bool hot: mouse.containsMouse || activeFocus
    signal toggled()

    readonly property int trackHeight: Math.max(Math.round(22 * ui.scale), Math.round(ui.controlHeight * 0.55))
    readonly property int trackWidth: Math.round(trackHeight * 1.9)
    readonly property int knobSize: Math.max(6, Math.round(trackHeight * 0.72))
    readonly property int knobInset: Math.max(1, Math.round((trackHeight - knobSize) / 2))

    implicitWidth: trackWidth + ui.s(12)
    implicitHeight: trackHeight + ui.s(12)
    activeFocusOnTab: interactive
    Keys.onSpacePressed: if (interactive) root.toggled()
    Keys.onReturnPressed: if (interactive) root.toggled()

    // The hover ring the shell draws around a control under the pointer.
    Rectangle {
        anchors.fill: parent
        visible: root.interactive && root.hot
        color: "transparent"
        radius: theme.radius
        border.width: 1
        border.color: root.activeFocus ? ui.accent : ui.alpha(ui.fg, 0.25)
    }

    Rectangle {
        id: track
        width: root.trackWidth
        height: root.trackHeight
        anchors.centerIn: parent
        radius: root.rounded ? height / 2 : 0
        color: root.checked ? ui.selectedFill : ui.normalFill
        border.width: root.checked ? 0 : 1
        border.color: ui.normalBorder
        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            width: root.knobSize
            height: root.knobSize
            radius: root.rounded ? height / 2 : 0
            x: root.checked !== root.mirrored ? track.width - width - root.knobInset : root.knobInset
            anchors.verticalCenter: parent.verticalCenter
            color: root.checked ? ui.fg : Qt.darker(ui.fg, 1.25)
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        enabled: root.interactive
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled()
    }
}
