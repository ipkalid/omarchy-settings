import QtQuick
import QtQuick.Layouts

// When something misbehaves: restart a part of the desktop, put a config
// back to Omarchy's, change the kernel, or start over.
SettingsPage {
    id: page
    pane: ""
    lists: ["system"]

    readonly property var sys: backend.lists.system || ({})

    SettingsGroup {
        separated: false
        title: qsTr("Restart")
        footer: qsTr("Restarting a part fixes most hiccups without logging out. Some open a terminal to show what they do.")
        Repeater {
            model: [
                { label: qsTr("The bar and notifications"), glyph: "\u{F06EB}", action: "restart.shell" },
                { label: qsTr("Sound"), glyph: "\u{F057E}", launch: "restartAudio" },
                { label: qsTr("Wi-Fi"), glyph: "\u{F05A9}", launch: "restartWifi" },
                { label: qsTr("Bluetooth"), glyph: "\u{F00AF}", launch: "restartBluetooth" },
                { label: qsTr("Trackpad"), glyph: "\u{F037D}", launch: "restartTrackpad" },
                { label: qsTr("Night Shift"), glyph: "\u{F0594}", action: "restart.nightLight" }
            ]
            delegate: ButtonRow {
                required property var modelData
                label: modelData.label
                glyph: modelData.glyph
                text: qsTr("Restart")
                onClicked: modelData.action ? backend.act(modelData.action, []) : backend.act("launch", [modelData.launch])
            }
        }
    }

    SettingsGroup {
        title: qsTr("Reset to Omarchy's Defaults")
        footer: qsTr("Puts a config back the way Omarchy ships it, after saving yours as a backup beside it. Settings' own changes come back on their own.")
        Repeater {
            model: [
                { label: qsTr("Hyprland"), detail: qsTr("Keybindings, monitors, input, and look in ~/.config/hypr"), launch: "refreshHyprland" },
                { label: qsTr("The bar and shell"), detail: qsTr("shell.json: the bar's layout, plugins, and idle times"), launch: "refreshShell" },
                { label: qsTr("Night Shift"), detail: qsTr("hyprsunset.conf"), launch: "refreshHyprsunset" },
                { label: qsTr("Boot screen"), detail: qsTr("The disk-unlock screen"), launch: "refreshPlymouth" },
                { label: qsTr("Tmux"), detail: qsTr("~/.config/tmux"), launch: "refreshTmux" }
            ]
            delegate: ButtonRow {
                required property var modelData
                label: modelData.label
                sublabel: modelData.detail
                text: qsTr("Reset…")
                onClicked: { reset.item = modelData; reset.open() }
            }
        }
    }

    SettingsGroup {
        visible: (page.sys.kernels || []).length > 1
        title: qsTr("Kernel")
        footer: qsTr("The kernel the computer starts with. Change it only to work around a problem; the others stay in the boot menu either way.")
        SettingRow {
            label: qsTr("Start with")
            ChoiceBox {
                id: kernel
                maxWidth: Math.round(240 * ui.scale)
                model: page.sys.kernels || []
                currentIndex: (page.sys.kernels || []).indexOf(page.sys.kernel)
                onActivated: {
                    if (currentText !== page.sys.kernel) { kernelDialog.target = currentText; kernelDialog.open() }
                    currentIndex = Qt.binding(() => (page.sys.kernels || []).indexOf(page.sys.kernel))
                }
                Accessible.name: qsTr("Kernel")
            }
        }
    }

    SettingsGroup {
        title: qsTr("Start Over")
        footer: qsTr("Erases your files, apps, and settings and puts the computer back to a fresh Omarchy install.")
        ButtonRow {
            label: qsTr("Reset this computer")
            glyph: "\u{F0026}"
            tint: ui.urgent
            text: qsTr("Reset Computer…")
            onClicked: { factory.typed = ""; factory.open() }
        }
    }

    OmaDialog {
        id: reset
        property var item: ({ label: "", launch: "" })
        title: qsTr("Reset %1 to Omarchy's defaults?").arg(item.label)
        acceptText: qsTr("Reset")
        onAccepted: backend.act("launch", [item.launch])
        Text {
            Layout.fillWidth: true
            text: qsTr("Your current config is kept as a backup beside the new one, so you can copy anything back.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }

    OmaDialog {
        id: kernelDialog
        property string target
        title: qsTr("Start with %1?").arg(target)
        acceptText: qsTr("Switch Kernel")
        onAccepted: backend.act("launch", ["kernel:" + target], ["system"])
        Text {
            Layout.fillWidth: true
            text: qsTr("A terminal opens to rebuild the boot entries and asks for your password. The new kernel is used after you restart; if it gives trouble, pick the old one from the boot menu.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }

    // Typing the word is the point: this can't be undone.
    OmaDialog {
        id: factory
        property string typed
        title: qsTr("Erase everything on this computer?")
        acceptText: qsTr("Erase and Reset")
        canAccept: typed.trim().toUpperCase() === "RESET"
        onAccepted: backend.act("launch", ["factoryReset"])
        Text {
            Layout.fillWidth: true
            text: qsTr("Your files, apps, and settings are deleted and Omarchy starts fresh. Back up anything you want to keep first. Type RESET to continue; a terminal then asks once more.")
            horizontalAlignment: Text.AlignLeft
            color: ui.urgent
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        OmaField {
            Layout.fillWidth: true
            placeholderText: "RESET"
            text: factory.typed
            onTextChanged: factory.typed = text
        }
    }
}
