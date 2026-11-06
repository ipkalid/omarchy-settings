import QtQuick

SettingsPage {
    id: page
    pane: "general"

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
}
