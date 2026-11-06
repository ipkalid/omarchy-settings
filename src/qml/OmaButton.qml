import QtQuick

// The Omarchy shell's button: monospace text (and an optional glyph) in a
// box that fills on hover and when selected. `bordered` draws its outline
// at rest, as the shell's choice buttons do.
Rectangle {
    id: root
    property string text
    property string glyph
    property string tooltip
    property bool bordered: true
    property bool highlighted: false
    property bool selected: false
    property bool flat: false
    property bool spinning: false
    signal clicked()

    readonly property bool hot: mouse.containsMouse || activeFocus
    implicitWidth: Math.max(ui.controlHeight, label.implicitWidth + icon.implicitWidth
                   + (glyph.length > 0 && text.length > 0 ? ui.s(6) : 0) + ui.s(10) * 2)
    implicitHeight: ui.controlHeight
    radius: theme.radius
    opacity: enabled ? 1 : 0.4
    color: mouse.pressed ? ui.pressedFill
         : (selected || highlighted) ? ui.selectedFill
         : hot ? ui.hoverFill
         : (bordered && !flat) ? ui.normalFill : "transparent"
    border.width: activeFocus || hot || (bordered && !flat) ? 1 : 0
    border.color: activeFocus ? ui.accent : hot ? ui.alpha(ui.fg, 0.25) : ui.normalBorder
    Behavior on color { ColorAnimation { duration: 100 } }

    activeFocusOnTab: enabled
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
    Accessible.role: Accessible.Button
    Accessible.name: text.length > 0 ? text : tooltip

    Row {
        anchors.centerIn: parent
        spacing: ui.s(6)
        Text {
            id: icon
            visible: root.glyph.length > 0
            text: root.glyph
            color: ui.fg
            font.family: ui.iconFont
            font.pixelSize: ui.icon
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
            RotationAnimation on rotation {
                running: root.spinning
                from: 0; to: 360
                duration: 900
                loops: Animation.Infinite
                onRunningChanged: if (!running) icon.rotation = 0
            }
        }
        Text {
            id: label
            visible: root.text.length > 0
            text: root.text
            color: root.highlighted ? ui.accent : ui.fg
            font.family: ui.font
            font.pixelSize: ui.body
            anchors.verticalCenter: parent.verticalCenter
            textFormat: Text.PlainText
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    OmaToolTip {
        visible: root.tooltip.length > 0 && mouse.containsMouse
        text: root.tooltip
    }
}
