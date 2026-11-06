import QtQuick

SettingsPage {
    id: page
    pane: "general"
    title: qsTr("General")
    tagline: backend.values["general.hostname"] || page.about.hostname || ""
    glyph: "\u{F0493}"
    description: qsTr("Manage how this computer is named, updated, and set up for your language and time zone.")
    lists: ["about", "languages"]

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
        NavRow {
            label: qsTr("Default Apps")
            glyph: "\u{F003B}"
            onOpen: win.openSub("GeneralApps", qsTr("Default Apps"))
        }
    }
}
