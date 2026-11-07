import QtQuick
import QtQuick.Layouts

// One row: an optional glyph and a label (with a note under it) on the
// reading side, the control on the other, like the rows of the shell's
// panels. A row that does something on click highlights under the pointer.
Item {
    id: row
    property string label
    property string sublabel
    property string glyph
    property string glyphFamily: ""
    // A picture in place of the glyph, such as an app's own icon.
    property string iconSource: ""
    property color tint: ui.fg
    property bool busy: false
    property bool dimmed: false
    property bool clickable: false
    property bool selected: false
    property alias controlSpacing: controls.spacing
    default property alias controls: controls.data
    signal activated()

    // Rows whose label matches the sidebar search are outlined, and the
    // first one is scrolled into view.
    readonly property bool matched: win.query.trim().length > 1
        && label.toLowerCase().indexOf(win.query.trim().toLowerCase()) >= 0
    onMatchedChanged: if (matched) Qt.callLater(() => win.revealRow(row))
    Component.onCompleted: if (matched) Qt.callLater(() => win.revealRow(row))

    width: parent ? parent.width : 0
    implicitHeight: Math.max(ui.rowHeight, content.implicitHeight + ui.s(10))
    opacity: dimmed ? 0.45 : 1

    // When the row is too narrow for its label beside the controls, the
    // controls move to a line of their own under the label, rather than
    // squeezing the label to a few letters or drawing over it.
    readonly property int textIndent: glyph.length > 0 || iconSource.length > 0 ? ui.s(18) + ui.s(12) : 0
    readonly property bool stacked: (label.length > 0 || sublabel.length > 0) && controls.implicitWidth > 0
        && width - ui.s(20) - textIndent - ui.s(12) - controls.implicitWidth < ui.s(140)

    Rectangle {
        anchors.fill: parent
        radius: theme.radius
        visible: row.selected || row.matched || (row.clickable && hover.hovered)
        color: row.selected ? ui.hoverFill : ui.normalFill
        border.width: row.matched ? 1 : 0
        border.color: ui.accent
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        // Inset like the shell's panel rows, so a highlighted row's fill
        // never crowds its text.
        anchors.leftMargin: ui.s(10)
        anchors.rightMargin: ui.s(10)
        spacing: ui.s(6)

        RowLayout {
            Layout.fillWidth: true
            spacing: ui.s(12)

            PaneIcon {
                visible: row.glyph.length > 0 && row.iconSource.length === 0
                glyph: row.glyph
                family: row.glyphFamily
                tint: row.tint
                Layout.preferredWidth: ui.s(18)
            }
            Image {
                visible: row.iconSource.length > 0
                source: row.iconSource
                Layout.preferredWidth: ui.s(18)
                Layout.preferredHeight: ui.s(18)
                sourceSize.width: ui.s(36)
                sourceSize.height: ui.s(36)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: ui.s(2)
                Text {
                    text: row.label
                    horizontalAlignment: Text.AlignLeft
                    color: ui.fg
                    font.family: ui.font
                    font.pixelSize: ui.body
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                }
                Text {
                    visible: row.sublabel.length > 0
                    text: row.sublabel
                    horizontalAlignment: Text.AlignLeft
                    color: ui.muted2
                    font.family: ui.font
                    font.pixelSize: ui.caption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                }
            }

            Spinner {
                running: row.busy
            }

            Item {
                id: inlineSlot
                visible: !row.stacked
                implicitWidth: controls.implicitWidth
                implicitHeight: controls.implicitHeight
                Layout.alignment: Qt.AlignVCenter
            }
        }

        Item {
            id: stackedSlot
            visible: row.stacked
            Layout.fillWidth: true
            Layout.leftMargin: row.textIndent
            implicitHeight: controls.implicitHeight
        }
    }

    Row {
        id: controls
        parent: row.stacked ? stackedSlot : inlineSlot
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: ui.s(8)
    }

    HoverHandler { id: hover; cursorShape: row.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor }
    TapHandler { onTapped: row.activated() }
}
