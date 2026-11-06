import QtQuick

// A row with a dropdown of choices for one setting. `options` is a list of
// {value, text}; a current value that is not among them (set by hand in a
// config file) is shown as it is rather than hidden.
SettingRow {
    id: row
    property string setting
    property var options: []
    property int menuWidth: Math.round(240 * ui.scale)
    readonly property var value: backend.values[setting]
    readonly property var model: {
        const list = options.slice()
        if (value !== undefined && value !== null && !list.some(o => o.value === value))
            list.push({ value: value, text: String(value) === "" ? qsTr("None") : String(value) })
        return list
    }

    busy: backend.busy.indexOf(setting) >= 0
    dimmed: value === undefined

    ChoiceBox {
        id: box
        enabled: row.value !== undefined
        maxWidth: row.menuWidth
        model: row.model
        textRole: "text"
        valueRole: "value"
        currentIndex: row.model.findIndex(o => o.value === row.value)
        onActivated: {
            backend.set(row.setting, currentValue)
            currentIndex = Qt.binding(() => row.model.findIndex(o => o.value === row.value))
        }
        Accessible.name: row.label
    }
}
