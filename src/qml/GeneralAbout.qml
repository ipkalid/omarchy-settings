import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

SettingsPage {
    id: page
    pane: "general"
    lists: ["about"]

    readonly property var about: backend.lists.about || ({})
    function gigabytes(bytes) {
        const _ = translator.language
        return qsTr("%1 GB").arg((bytes / 1e9).toLocaleString(Qt.locale(), 'f', 0))
    }
    function duration(seconds) {
        const days = Math.floor(seconds / 86400), hours = Math.floor(seconds % 86400 / 3600)
        if (days > 0) return qsTr("%n days", "", days) + ", " + qsTr("%n hours", "", hours)
        return qsTr("%n hours", "", hours) + ", " + qsTr("%n minutes", "", Math.floor(seconds % 3600 / 60))
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
        spacing: 6
        PaneIcon {
            Layout.alignment: Qt.AlignHCenter
            glyph: "\u{F0322}"
            tint: ui.accent
            size: 64
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: backend.values["general.hostname"] || page.about.hostname || ""
            color: ui.fg
            font.pixelSize: ui.title
            font.weight: Font.DemiBold
            textFormat: Text.PlainText
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: page.about.model || ""
            color: ui.muted
            textFormat: Text.PlainText
        }
    }

    SettingsGroup {
        footer: qsTr("Others see this name on your network. Changing it asks for your password.")
        SettingRow {
            label: qsTr("Name")
            busy: backend.busy.indexOf("general.hostname") >= 0
            OmaField {
                id: name
                width: 220
                text: backend.values["general.hostname"] || ""
                onAccepted: if (text !== backend.values["general.hostname"]) backend.set("general.hostname", text)
                onActiveFocusChanged: if (!activeFocus) text = Qt.binding(() => backend.values["general.hostname"] || "")
                Accessible.name: qsTr("Computer name")
            }
        }
    }

    SettingsGroup {
        InfoRow { label: qsTr("Processor"); value: page.about.cpu || "" }
        InfoRow { label: qsTr("Graphics"); value: page.about.gpu || "" }
        InfoRow {
            label: qsTr("Memory")
            value: page.about.memoryGb ? qsTr("%1 GB").arg(Math.round(page.about.memoryGb).toLocaleString(Qt.locale(), 'f', 0)) : ""
        }
        InfoRow {
            label: qsTr("Storage")
            value: page.about.diskTotal ? qsTr("%1 available of %2").arg(page.gigabytes(page.about.diskFree)).arg(page.gigabytes(page.about.diskTotal)) : ""
        }
    }

    SettingsGroup {
        InfoRow { label: qsTr("Omarchy"); value: page.about.omarchy || "" }
        InfoRow { label: qsTr("Update channel"); value: page.about.channel || "" }
        InfoRow { label: qsTr("Kernel"); value: page.about.kernel || "" }
        InfoRow { label: qsTr("Up for"); value: page.about.uptimeSeconds ? page.duration(page.about.uptimeSeconds) : "" }
        ButtonRow {
            label: qsTr("Omarchy's own About screen")
            text: qsTr("Open")
            onClicked: backend.act("about.open", [])
        }
    }
}
