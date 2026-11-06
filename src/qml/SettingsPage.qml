import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A pane's scrolling page. It opens with a hero like the shell's panels:
// the glyph, the name in bold, an uppercase line under it, and the pane's
// main switch on the far side (Wi-Fi, Bluetooth).
ScrollView {
    id: page
    property string pane
    property string title
    property string glyph
    property string tagline
    property string description
    property string heroSetting
    // Lists to load when the page opens, and how often to refresh live data.
    property var lists: []
    // Settings from other panes this page also shows.
    property var settings: []
    property int pollMs: 0
    default property alias content: column.data

    function reload() {
        if (pane.length > 0)
            backend.refreshPane(pane)
        for (const list of lists)
            backend.load(list)
        for (const id of settings)
            backend.refresh(id)
    }

    contentWidth: availableWidth
    contentHeight: column.implicitHeight + ui.s(40)
    clip: true
    Component.onCompleted: reload()

    Connections {
        target: win
        function onActiveChanged() { if (win.active) page.reload() }
    }
    Timer {
        interval: Math.max(1000, page.pollMs)
        running: page.pollMs > 0 && win.active
        repeat: true
        onTriggered: page.reload()
    }

    ColumnLayout {
        id: column
        width: Math.min(page.availableWidth - ui.s(56), Math.round(640 * ui.scale))
        x: (page.availableWidth - width) / 2
        spacing: ui.s(18)

        Item { implicitHeight: ui.s(6) }

        RowLayout {
            visible: page.glyph.length > 0
            Layout.fillWidth: true
            spacing: ui.s(14)

            PaneIcon {
                glyph: page.glyph
                size: ui.display
                Layout.alignment: Qt.AlignVCenter
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: ui.s(2)
                Text {
                    text: page.title
                    horizontalAlignment: Text.AlignLeft
                    color: ui.fg
                    font.family: ui.font
                    font.pixelSize: ui.title
                    font.bold: true
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                }
                Text {
                    visible: page.tagline.length > 0
                    text: page.tagline.toLocaleUpperCase(Qt.locale())
                    horizontalAlignment: Text.AlignLeft
                    color: ui.muted
                    font.family: ui.font
                    font.pixelSize: ui.caption
                    font.bold: true
                    // Spacing letters apart breaks up joined scripts like Arabic.
                    font.letterSpacing: Qt.application.layoutDirection === Qt.RightToLeft ? 0 : 1
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                }
            }
            Spinner { running: page.heroSetting.length > 0 && backend.busy.indexOf(page.heroSetting) >= 0 }
            OmaToggle {
                visible: page.heroSetting.length > 0
                checked: !!backend.values[page.heroSetting]
                interactive: backend.values[page.heroSetting] !== undefined
                onToggled: backend.set(page.heroSetting, !backend.values[page.heroSetting])
                Accessible.role: Accessible.CheckBox
                Accessible.name: page.title
                Accessible.checked: checked
            }
        }

        Text {
            visible: page.description.length > 0
            Layout.fillWidth: true
            Layout.topMargin: -ui.s(6)
            text: page.description
            horizontalAlignment: Text.AlignLeft
            color: ui.muted2
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
