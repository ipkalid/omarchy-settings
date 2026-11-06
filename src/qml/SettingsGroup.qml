import QtQuick
import QtQuick.Layouts

// A section of a pane, as the shell's panels lay them out: a thin separator
// above, an uppercase caption, its rows, and an optional note.
ColumnLayout {
    id: group
    property string title
    property string footer
    property bool separated: true
    // A collapsible group's caption is a button that shows or hides its rows,
    // with `summary` beside it to say what's inside while it's closed.
    property bool collapsible: false
    property bool collapsed: false
    property string summary
    signal toggle()
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

    RowLayout {
        id: header
        visible: group.title.length > 0
        Layout.fillWidth: true
        spacing: ui.s(8)
        activeFocusOnTab: group.collapsible
        Keys.onReturnPressed: if (group.collapsible) group.toggle()
        Keys.onSpacePressed: if (group.collapsible) group.toggle()
        Accessible.role: group.collapsible ? Accessible.Button : Accessible.StaticText
        Accessible.name: group.title
        Accessible.description: group.summary

        Text {
            text: group.title.toLocaleUpperCase(Qt.locale())
            horizontalAlignment: Text.AlignLeft
            color: header.activeFocus || hover.hovered ? ui.fg : ui.muted
            font.family: ui.font
            font.pixelSize: ui.caption
            font.bold: true
            // Spacing letters apart breaks up joined scripts like Arabic.
            font.letterSpacing: Qt.application.layoutDirection === Qt.RightToLeft ? 0 : 0.5
            Layout.fillWidth: !group.collapsible
            textFormat: Text.PlainText
        }
        Text {
            // Also the spacer that keeps the chevron at the far edge.
            visible: group.collapsible
            text: group.summary
            horizontalAlignment: Text.AlignLeft
            color: ui.muted2
            font.family: ui.font
            font.pixelSize: ui.caption
            Layout.fillWidth: true
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        // Points down when open, and along the reading direction when closed.
        PaneIcon {
            visible: group.collapsible
            glyph: !group.collapsed ? "\u{F0140}"
                   : Qt.application.layoutDirection === Qt.RightToLeft ? "\u{F0141}" : "\u{F0142}"
            tint: header.activeFocus || hover.hovered ? ui.fg : ui.muted
            size: ui.s(16)
        }

        HoverHandler { id: hover; enabled: group.collapsible; cursorShape: Qt.PointingHandCursor }
        TapHandler { enabled: group.collapsible; onTapped: group.toggle() }
    }

    Column {
        id: list
        visible: !(group.collapsible && group.collapsed)
        Layout.fillWidth: true
        spacing: ui.s(2)
    }

    Text {
        visible: group.footer.length > 0 && !(group.collapsible && group.collapsed)
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
