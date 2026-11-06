import QtQuick
import QtQuick.Layouts

// The ASCII art Omarchy shows on the screensaver or the About screen, with
// the ways to change it: edit the text, make it from a picture, or go back
// to Omarchy's.
SettingsGroup {
    id: group
    property string which: "screensaver"
    readonly property string art: (backend.lists.branding || {})[which] || ""

    Rectangle {
        width: parent.width
        implicitHeight: Math.min(artText.implicitHeight, ui.s(220)) + ui.s(24)
        radius: theme.radius
        color: ui.normalFill
        border.width: 1
        border.color: ui.normalBorder
        clip: true
        Text {
            id: artText
            anchors.centerIn: parent
            text: group.art
            color: ui.accent
            font.family: ui.iconFont
            // Small enough that the widest line fits.
            font.pixelSize: Math.max(5, Math.min(ui.small, Math.floor((parent.width - ui.s(24)) / Math.max(1, group.longest) / 0.6)))
            lineHeight: 1.0
            textFormat: Text.PlainText
            LayoutMirroring.enabled: false
        }
    }
    readonly property int longest: art.split("\n").reduce((n, l) => Math.max(n, l.length), 0)

    SettingRow {
        label: ""
        OmaButton {
            text: qsTr("Edit Text…")
            onClicked: backend.act("branding.edit", [group.which])
        }
        OmaButton {
            text: qsTr("From a Picture…")
            onClicked: backend.act("branding.image", [group.which])
        }
        OmaButton {
            text: qsTr("Restore Omarchy's")
            bordered: false
            onClicked: backend.act("branding.reset", [group.which], ["branding"])
        }
    }
}
