import QtQuick
import QtQuick.Layouts

// How full the disks are, what in your home folder takes the room, and the
// system's snapshots.
SettingsPage {
    id: page
    pane: "storage"
    title: qsTr("Storage")
    tagline: page.system ? qsTr("%1 free").arg(page.size(page.system.free)) : ""
    glyph: "\u{F02CA}"
    description: qsTr("How full your disks are, what takes up the room, and the system's snapshots.")
    lists: ["storage"]

    readonly property var storage: backend.lists.storage || ({})
    readonly property var system: (storage.volumes || []).find(v => v.path === "/")
    readonly property var home: backend.lists.homeUsage
    readonly property var snapshots: backend.lists.snapshots
    function size(bytes) {
        const _ = translator.language
        if (bytes >= 1e12) return qsTr("%1 TB").arg((bytes / 1e12).toLocaleString(Qt.locale(), 'f', 1))
        if (bytes >= 1e9) return qsTr("%1 GB").arg((bytes / 1e9).toLocaleString(Qt.locale(), 'f', bytes >= 1e11 ? 0 : 1))
        return qsTr("%1 MB").arg((bytes / 1e6).toLocaleString(Qt.locale(), 'f', 0))
    }

    SettingsGroup {
        title: qsTr("Disks")
        Repeater {
            model: page.storage.volumes || []
            delegate: ColumnLayout {
                required property var modelData
                width: parent.width
                spacing: ui.s(4)
                SettingRow {
                    Layout.fillWidth: true
                    label: modelData.name
                    sublabel: modelData.path + " · " + modelData.fs
                    Text {
                        text: qsTr("%1 free of %2").arg(page.size(modelData.free)).arg(page.size(modelData.total))
                        color: ui.fg
                        font.family: ui.font
                        font.pixelSize: ui.body
                        anchors.verticalCenter: parent.verticalCenter
                        textFormat: Text.PlainText
                    }
                }
                // How full it is, drawn like the shell's sliders.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: ui.s(10)
                    Layout.rightMargin: ui.s(10)
                    Layout.bottomMargin: ui.s(8)
                    height: Math.max(4, ui.s(5))
                    radius: height / 2
                    color: ui.selectedFill
                    Rectangle {
                        readonly property real used: 1 - modelData.free / modelData.total
                        x: Qt.application.layoutDirection === Qt.RightToLeft ? parent.width - width : 0
                        width: parent.width * used
                        height: parent.height
                        radius: height / 2
                        color: used > 0.9 ? ui.urgent : ui.fg
                    }
                }
            }
        }
    }

    SettingsGroup {
        ButtonRow {
            label: qsTr("Disk speed")
            sublabel: qsTr("How fast your disk reads and writes")
            glyph: "\u{F04C5}"
            text: qsTr("Test…")
            onClicked: backend.act("summon", ["omarchy.disk-speedtest"])
        }
    }

    SettingsGroup {
        title: qsTr("Home Folder")
        footer: qsTr("Folders over 50 MB. Adding them up can take a minute on a big disk.")
        ButtonRow {
            label: page.home ? qsTr("Largest folders in your home") : qsTr("See what takes up space")
            text: page.home ? qsTr("Check Again") : qsTr("Check")
            busy: backend.busy.indexOf("list:homeUsage") >= 0
            onClicked: backend.load("homeUsage")
        }
        Repeater {
            model: page.home ? page.home.folders : []
            delegate: InfoRow {
                required property var modelData
                label: modelData.name
                glyph: "\u{F024B}"
                value: page.size(modelData.bytes)
            }
        }
        ButtonRow {
            visible: !!page.home
            label: qsTr("Package cache")
            sublabel: page.home ? qsTr("%1 of downloaded packages kept for downgrades").arg(page.size(page.home.packageCache)) : ""
            text: qsTr("Clean Up…")
            onClicked: backend.act("launch", ["pkgPrune"])
        }
    }

    SettingsGroup {
        visible: page.storage.snapper === true
        title: qsTr("Snapshots")
        footer: qsTr("Snapshots of the system are taken before updates, so a bad one can be undone from the boot menu. Seeing and making them asks for your password.")
        ButtonRow {
            label: page.snapshots ? (page.snapshots.ok ? qsTr("%n snapshots", "", page.snapshots.snapshots.length) : page.snapshots.error)
                                  : qsTr("System snapshots")
            text: page.snapshots ? qsTr("Refresh") : qsTr("Show")
            busy: backend.busy.indexOf("list:snapshots") >= 0
            onClicked: backend.load("snapshots")
        }
        Repeater {
            model: page.snapshots && page.snapshots.ok ? page.snapshots.snapshots.slice(0, 15) : []
            delegate: InfoRow {
                required property var modelData
                label: modelData.description || qsTr("Snapshot %1").arg(modelData.number)
                glyph: "\u{F02DA}"
                value: modelData.date
            }
        }
        ButtonRow {
            label: qsTr("Take a snapshot now")
            text: qsTr("Take Snapshot")
            busy: backend.busy.indexOf("snapshot.create") >= 0
            onClicked: backend.act("snapshot.create", [], page.snapshots ? ["snapshots"] : [])
        }
    }
}
