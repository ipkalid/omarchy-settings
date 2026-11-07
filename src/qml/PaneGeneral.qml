import QtQuick

SettingsPage {
    id: page
    pane: "general"
    title: qsTr("General")
    tagline: backend.values["general.hostname"] || page.about.hostname || ""
    glyph: "\u{F0493}"
    description: qsTr("Manage how this computer is named, updated, and set up for your language and time zone.")
    lists: ["about", "languages", "sync"]

    readonly property var about: backend.lists.about || ({})
    readonly property var language: (backend.lists.languages || []).find(l => l.code === backend.values["general.language"])

    SettingsGroup {
        NavRow {
            label: qsTr("About")
            glyph: "\u{F02FC}"
            detail: page.about.hostname || ""
            onOpen: win.openSub("GeneralAbout", qsTr("About"))
        }
        NavRow {
            label: qsTr("Software Update")
            glyph: "\u{F06B0}"
            detail: page.about.omarchy ? qsTr("Omarchy %1").arg(page.about.omarchy) : ""
            onOpen: win.openSub("GeneralUpdate", qsTr("Software Update"))
        }
    }

    SettingsGroup {
        NavRow {
            label: qsTr("Language & Region")
            glyph: "\u{F05CA}"
            detail: page.language ? page.language.name : ""
            onOpen: win.openSub("GeneralLanguage", qsTr("Language & Region"))
        }
        NavRow {
            label: qsTr("Date & Time")
            glyph: "\u{F0954}"
            detail: backend.values["general.timezone"] || ""
            onOpen: win.openSub("GeneralDateTime", qsTr("Date & Time"))
        }
    }

    SettingsGroup {
        NavRow {
            label: qsTr("Login Items")
            glyph: "\u{F0463}"
            onOpen: win.openSub("GeneralLoginItems", qsTr("Login Items"))
        }
        NavRow {
            label: qsTr("Web Apps")
            glyph: "\u{F059F}"
            onOpen: win.openSub("GeneralWebApps", qsTr("Web Apps"))
        }
        NavRow {
            label: qsTr("Default Apps")
            glyph: "\u{F003B}"
            onOpen: win.openSub("GeneralApps", qsTr("Default Apps"))
        }
    }

    SettingsGroup {
        NavRow {
            label: qsTr("Sync")
            sublabel: qsTr("Keep settings and apps in a GitHub repository, and bring another computer to match")
            glyph: "\u{F04E6}"
            detail: (backend.lists.sync || {}).pending ? qsTr("Newer settings") : ""
            onOpen: win.openSub("GeneralSync", qsTr("Sync"))
        }
        NavRow {
            label: qsTr("Troubleshooting")
            sublabel: qsTr("Restart parts of the desktop, reset configs, kernel, start over")
            glyph: "\u{F0BC6}"
            onOpen: win.openSub("GeneralTroubleshooting", qsTr("Troubleshooting"))
        }
        NavRow {
            label: qsTr("Changes & Backup")
            sublabel: qsTr("What Settings changed, export and import, and how to open Settings")
            glyph: "\u{F006F}"
            onOpen: win.openSub("GeneralSettingsApp", qsTr("Changes & Backup"))
        }
    }
}
