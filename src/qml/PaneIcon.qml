import QtQuick

// A pane's glyph, drawn plain the way the Omarchy shell draws its icons.
Text {
    property string glyph
    property color tint: ui.fg
    property int size: ui.icon
    // Omarchy's menu draws some brand icons from its own "omarchy" font.
    property string family: ""

    text: glyph
    color: tint
    font.family: family.length > 0 ? family : ui.iconFont
    font.pixelSize: size
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    textFormat: Text.PlainText
}
