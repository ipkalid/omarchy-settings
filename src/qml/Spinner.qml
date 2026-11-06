import QtQuick

// Work in progress, shown the way the shell shows it: a turning glyph.
Text {
    id: spinner
    property bool running: true
    visible: running
    text: "\u{F0450}"
    color: ui.muted
    font.family: ui.iconFont
    font.pixelSize: ui.icon
    textFormat: Text.PlainText
    RotationAnimation on rotation {
        running: spinner.running && spinner.visible
        from: 0; to: 360
        duration: 900
        loops: Animation.Infinite
    }
}
