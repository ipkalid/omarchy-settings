import QtQuick
import QtQuick.Controls

SettingsPage {
    id: page
    pane: "sound"
    title: qsTr("Sound")
    tagline: backend.values["sound.outputMuted"] === true ? qsTr("Muted") : backend.values["sound.outputVolume"] !== undefined ? qsTr("Volume %1").arg(page.percent(backend.values["sound.outputVolume"])) : ""
    glyph: "\u{F057E}"
    description: qsTr("Pick where sound plays and which microphone listens, and how loud each is.")
    lists: ["sinks", "sources"]
    pollMs: 4000

    function percent(v) { return qsTr("%1%").arg(Math.round(v).toLocaleString(Qt.locale(), 'f', 0)) }

    SettingsGroup {
        title: qsTr("Output")
        ChoiceRow {
            setting: "sound.output"
            label: qsTr("Play sound through")
            menuWidth: 300
            options: (backend.lists.sinks || []).map(d => ({ value: d.name, text: d.description }))
        }
        SliderRow {
            setting: "sound.outputVolume"
            label: qsTr("Output volume")
            to: 100
            live: true
            format: page.percent
        }
        ToggleRow {
            setting: "sound.outputMuted"
            label: qsTr("Mute")
        }
    }

    SettingsGroup {
        title: qsTr("Input")
        ChoiceRow {
            setting: "sound.input"
            label: qsTr("Listen through")
            menuWidth: 300
            options: (backend.lists.sources || []).map(d => ({ value: d.name, text: d.description }))
        }
        SliderRow {
            setting: "sound.inputVolume"
            label: qsTr("Input volume")
            to: 100
            live: true
            format: page.percent
        }
        ToggleRow {
            setting: "sound.inputMuted"
            label: qsTr("Mute")
        }
    }
}
