import QtQuick

SettingsPage {
    id: page
    pane: "general"
    lists: ["fileTypes", "software", "defaultApps"]

    readonly property var typeNames: ({
        images: qsTr("Pictures"), videos: qsTr("Videos"), music: qsTr("Music"), pdf: qsTr("PDFs"),
        text: qsTr("Text files"), folders: qsTr("Folders"), archives: qsTr("Archives"), email: qsTr("Email links")
    })

    // Every menu lists only what's installed; Apps installs the rest. The
    // browser, terminal, and editor are installed when the command Omarchy
    // checks for is there, the agents and dictation as the Apps pane finds.
    readonly property var commands: backend.lists.defaultApps || ({})
    function installedOf(kind, options) {
        const have = page.commands[kind] || []
        return options.filter(o => have.indexOf(o.value) >= 0)
    }
    function installedIn(category) {
        return (backend.lists.software || []).filter(i => i.installed && i.category === category)
                                             .map(i => ({ value: i.key.slice(category.length + 1), text: i.label }))
    }

    SettingsGroup {
        ChoiceRow {
            setting: "general.browser"
            label: qsTr("Web browser")
            options: page.installedOf("browser", [
                { value: "chromium", text: "Chromium" }, { value: "chrome", text: "Google Chrome" },
                { value: "brave", text: "Brave" }, { value: "brave-origin", text: "Brave Origin" },
                { value: "edge", text: "Microsoft Edge" }, { value: "firefox", text: "Firefox" }, { value: "zen", text: "Zen" }
            ])
        }
        ChoiceRow {
            setting: "general.terminal"
            label: qsTr("Terminal")
            options: page.installedOf("terminal", [
                { value: "alacritty", text: "Alacritty" }, { value: "foot", text: "Foot" },
                { value: "ghostty", text: "Ghostty" }, { value: "kitty", text: "Kitty" }
            ])
        }
        ChoiceRow {
            setting: "general.editor"
            label: qsTr("Text editor")
            options: page.installedOf("editor", [
                { value: "nvim", text: "Neovim" }, { value: "code", text: "VS Code" }, { value: "cursor", text: "Cursor" },
                { value: "zed", text: "Zed" }, { value: "sublime_text", text: "Sublime Text" }, { value: "helix", text: "Helix" },
                { value: "vim", text: "Vim" }, { value: "emacs", text: "Emacs" }
            ])
        }
    }

    SettingsGroup {
        title: qsTr("Tools")
        footer: qsTr("The coding agent opens with Omarchy's agent shortcut; dictation types what you say.")
        ChoiceRow {
            setting: "general.agent"
            label: qsTr("Coding agent")
            menuWidth: Math.round(220 * ui.scale)
            options: page.installedIn("agent")
        }
        ChoiceRow {
            setting: "general.dictation"
            label: qsTr("Dictation")
            options: page.installedIn("dictation")
        }
    }

    SettingsGroup {
        footer: qsTr("Each menu lists only what's installed.")
        NavRow {
            label: qsTr("Install more")
            glyph: "\u{F003B}"
            detail: qsTr("Apps")
            onOpen: win.go("software")
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
