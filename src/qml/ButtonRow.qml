import QtQuick

// A row that does something: opens a setup, runs a check.
SettingRow {
    id: row
    property string text
    property bool highlighted: false
    signal clicked()

    OmaButton {
        text: row.text
        highlighted: row.highlighted
        enabled: !row.busy
        onClicked: row.clicked()
    }
}
