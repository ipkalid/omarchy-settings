import QtQuick

// A row whose toggle reads and writes one setting. Clicking anywhere on
// the row flips it, as the shell's toggle rows do.
SettingRow {
    id: row
    property string setting
    readonly property var value: backend.values[setting]

    busy: backend.busy.indexOf(setting) >= 0
    dimmed: value === undefined
    onActivated: if (value !== undefined) backend.set(setting, !value)

    OmaToggle {
        checked: !!row.value
        interactive: row.value !== undefined
        onToggled: backend.set(row.setting, !row.value)
        Accessible.role: Accessible.CheckBox
        Accessible.name: row.label
        Accessible.checked: checked
    }
}
