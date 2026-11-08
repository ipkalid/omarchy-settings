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
    // The pane whose settings Restore Defaults puts back to Omarchy's.
    property string resetPane: ""
    // The config files this page's settings live in, by their key in the
    // backend's table, the main one first. They are offered at the bottom
    // to open in the person's editor, and Ctrl+E opens the first.
    property var configFiles: []
    readonly property var shownConfigFiles: configFiles.length === 0 ? []
        : configFiles.map(k => (backend.lists.configFiles || []).find(f => f.key === k)).filter(f => f && f.exists)
    default property alias content: column.data

    function reload() {
        if (pane.length > 0)
            backend.refreshPane(pane)
        for (const list of lists)
            backend.load(list)
        for (const id of settings)
            backend.refresh(id)
        if (configFiles.length > 0)
            backend.load("configFiles")
    }

    // Opens the page's main config file; false when it has none.
    function openConfig() {
        if (shownConfigFiles.length === 0)
            return false
        backend.act("edit", [shownConfigFiles[0].key])
        return true
    }

    // Scrolls so `item` shows, a little below the top.
    function reveal(item) {
        const y = item.mapToItem(column, 0, 0).y
        const flick = page.contentItem
        flick.contentY = Math.max(0, Math.min(y - ui.s(60), flick.contentHeight - flick.height))
    }

    contentWidth: availableWidth
    contentHeight: outer.implicitHeight + ui.s(40)
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
      id: outer
      width: Math.min(page.availableWidth - (page.availableWidth < ui.s(500) ? ui.s(28) : ui.s(56)), Math.round(640 * ui.scale))
      x: (page.availableWidth - width) / 2
      spacing: ui.s(18)

      ColumnLayout {
        id: column
        Layout.fillWidth: true
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

      // The files behind this page, for anything it can't change itself.
      SettingsGroup {
        visible: page.shownConfigFiles.length > 0
        title: qsTr("Config Files")
        footer: qsTr("Opens in your default editor, chosen in General › Default Apps.")
        Repeater {
          model: page.shownConfigFiles
          delegate: ButtonRow {
            required property var modelData
            glyph: "\u{F0DC9}"
            label: modelData.name
            sublabel: modelData.path
            text: qsTr("Open…")
            onClicked: backend.act("edit", [modelData.key])
          }
        }
      }

      // Restore Defaults, below everything a pane shows.
      SettingsGroup {
        visible: page.resetPane.length > 0
        ButtonRow {
          label: qsTr("Restore Omarchy's defaults")
          sublabel: qsTr("Puts this page's settings back the way Omarchy ships them")
          glyph: "\u{F099B}"
          text: qsTr("Restore Defaults…")
          onClicked: { backend.load("defaults:" + page.resetPane); restoreDialog.open() }
        }
      }
    }

    OmaDialog {
        id: restoreDialog
        objectName: "restoreDialog"
        readonly property var changes: backend.lists["defaults:" + page.resetPane] || []
        readonly property bool loading: backend.busy.indexOf("list:defaults:" + page.resetPane) >= 0
        title: qsTr("Restore Omarchy's defaults?")
        acceptText: qsTr("Restore")
        canAccept: !loading && changes.length > 0
        onAccepted: backend.act("pane.reset", [page.resetPane],
                                ["defaults:" + page.resetPane, "bar", "dock", "monitors", "fonts"], page.resetPane)
        function shown(v) {
            if (v === true) return qsTr("On")
            if (v === false) return qsTr("Off")
            return String(v).split("\n").join(" ")
        }
        Text {
            Layout.fillWidth: true
            text: restoreDialog.loading ? qsTr("Checking…")
                  : restoreDialog.changes.length === 0 ? qsTr("Everything here is already as Omarchy ships it.")
                  : qsTr("These change back:")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        Repeater {
            model: restoreDialog.loading ? [] : restoreDialog.changes
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                text: qsTranslate("Setting", modelData.label) + ": "
                      + (String(modelData.value).length > 0 ? restoreDialog.shown(modelData.value) + " → " : "")
                      + restoreDialog.shown(modelData["default"])
                horizontalAlignment: Text.AlignLeft
                color: ui.fg
                font.family: ui.font
                font.pixelSize: ui.small
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }
        }
    }
}
