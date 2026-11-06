import QtQuick

SettingsPage {
    id: page
    pane: "general"
    lists: ["fileTypes"]

    readonly property var typeNames: ({
        images: qsTr("Pictures"), videos: qsTr("Videos"), music: qsTr("Music"), pdf: qsTr("PDFs"),
        text: qsTr("Text files"), folders: qsTr("Folders"), archives: qsTr("Archives"), email: qsTr("Email links")
    })

    SettingsGroup {
        footer: qsTr("Choosing an app that isn't installed opens a terminal to install it first.")
        ChoiceRow {
            setting: "general.browser"
            label: qsTr("Web browser")
            options: [
                { value: "chromium", text: "Chromium" }, { value: "chrome", text: "Google Chrome" },
                { value: "brave", text: "Brave" }, { value: "brave-origin", text: "Brave Origin" },
                { value: "edge", text: "Microsoft Edge" }, { value: "firefox", text: "Firefox" }, { value: "zen", text: "Zen" }
            ]
        }
        ChoiceRow {
            setting: "general.terminal"
            label: qsTr("Terminal")
            options: [
                { value: "alacritty", text: "Alacritty" }, { value: "foot", text: "Foot" },
                { value: "ghostty", text: "Ghostty" }, { value: "kitty", text: "Kitty" }
            ]
        }
        ChoiceRow {
            setting: "general.editor"
            label: qsTr("Text editor")
            options: [
                { value: "nvim", text: "Neovim" }, { value: "code", text: "VS Code" }, { value: "cursor", text: "Cursor" },
                { value: "zed", text: "Zed" }, { value: "sublime_text", text: "Sublime Text" }, { value: "helix", text: "Helix" },
                { value: "vim", text: "Vim" }, { value: "emacs", text: "Emacs" }
            ]
        }
    }

    SettingsGroup {
        title: qsTr("Open Files With")
        footer: qsTr("The app that opens each kind of file when you double-click it or open it from another app.")
        Repeater {
            model: backend.lists.fileTypes || []
            delegate: SettingRow {
                id: typeRow
                required property var modelData
                label: page.typeNames[modelData.type] || modelData.type
                busy: backend.busy.indexOf("mime.set") >= 0
                ChoiceBox {
                    readonly property var options: {
                        const list = typeRow.modelData.apps.map(a => ({ value: a.id, text: a.name }))
                        if (typeRow.modelData.current && !list.some(o => o.value === typeRow.modelData.current))
                            list.unshift({ value: typeRow.modelData.current, text: typeRow.modelData.current })
                        return list
                    }
                    maxWidth: Math.round(240 * ui.scale)
                    model: options
                    textRole: "text"
                    valueRole: "value"
                    currentIndex: options.findIndex(o => o.value === typeRow.modelData.current)
                    placeholder: qsTr("Nothing set")
                    onActivated: backend.act("mime.set", [typeRow.modelData.type, currentValue], ["fileTypes"])
                    Accessible.name: typeRow.label
                }
            }
        }
    }
}
