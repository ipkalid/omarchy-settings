import QtQuick
import QtQuick.Layouts

// A row with a slider for a numeric setting, with words at either end
// (Slow, Fast). `live` writes while dragging, for things you want to hear or
// see change (volume, brightness).
SettingRow {
    id: row
    property string setting
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property bool live: false
    property string fromText
    property string toText
    property var format: null
    readonly property var value: backend.values[setting]

    busy: backend.busy.indexOf(setting) >= 0
    dimmed: value === undefined

    ColumnLayout {
        spacing: 0
        width: Math.round(250 * ui.scale)

        RowLayout {
            Layout.fillWidth: true
            spacing: ui.s(8)
            OmaSlider {
                id: slider
                Layout.fillWidth: true
                enabled: row.value !== undefined
                from: row.from
                to: row.to
                stepSize: row.stepSize
                snapMode: OmaSlider.SnapAlways
                value: row.value === undefined ? row.from : row.value
                onMoved: if (row.live) backend.set(row.setting, value)
                onPressedChanged: {
                    if (!pressed) {
                        backend.set(row.setting, value)
                        value = Qt.binding(() => row.value === undefined ? row.from : row.value)
                    }
                }
                Keys.onReleased: event => {
                    if (!event.isAutoRepeat && [Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_PageUp,
                                                Qt.Key_PageDown, Qt.Key_Home, Qt.Key_End].indexOf(event.key) >= 0)
                        backend.set(row.setting, value)
                }
                Accessible.name: row.label
            }
            Text {
                visible: row.format !== null
                text: row.format ? row.format(slider.value) : ""
                color: ui.fg
                font.family: ui.font
                font.pixelSize: ui.small
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: ui.s(56)
                textFormat: Text.PlainText
            }
        }
        RowLayout {
            visible: row.fromText.length > 0 || row.toText.length > 0
            Layout.fillWidth: true
            Layout.rightMargin: row.format !== null ? ui.s(64) : 0
            Text { text: row.fromText; color: ui.muted; font.family: ui.font; font.pixelSize: ui.caption; textFormat: Text.PlainText }
            Item { Layout.fillWidth: true }
            Text { text: row.toText; color: ui.muted; font.family: ui.font; font.pixelSize: ui.caption; textFormat: Text.PlainText }
        }
    }
}
