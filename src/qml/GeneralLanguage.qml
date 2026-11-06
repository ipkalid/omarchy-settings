import QtQuick

SettingsPage {
    id: page
    pane: "general"
    lists: ["languages"]

    SettingsGroup {
        title: qsTr("Language")
        footer: qsTr("Omarchy and apps that follow it switch right away. Other apps switch the next time you log in.")
        ChoiceRow {
            setting: "general.language"
            label: qsTr("Preferred language")
            menuWidth: 260
            options: (backend.lists.languages || []).map(l => ({
                value: l.code, text: l.name === l.englishName ? l.name : l.name + " — " + l.englishName }))
        }
    }

    SettingsGroup {
        title: qsTr("Typing")
        footer: qsTr("Keyboard layouts are in Keyboard. Input methods add Chinese, Japanese, and Korean typing.")
        NavRow {
            label: qsTr("Keyboard")
            glyph: "\u{F030C}"
            onOpen: win.go("keyboard")
        }
        ButtonRow {
            label: qsTr("Input methods")
            text: qsTr("Set Up…")
            onClicked: backend.act("launch", ["inputMethods"])
        }
    }
}
