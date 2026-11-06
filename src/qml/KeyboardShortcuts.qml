import QtQuick
import QtQuick.Layouts

// Every keyboard shortcut, the ones you made, and the ones you turned off.
// New and changed shortcuts go into the generated Hyprland file through
// o.rebind, so they replace whatever had the same keys.
SettingsPage {
    id: page
    pane: ""
    lists: ["shortcuts", "apps"]

    readonly property var info: backend.lists.shortcuts || ({})
    readonly property var custom: info.custom || []
    readonly property var disabled: info.disabled || []
    readonly property var listed: info.listed || []
    readonly property string query: filter.text.trim().toLowerCase()
    readonly property var shown: query.length === 0 ? listed
        : listed.filter(b => (b.keys + " " + b.action).toLowerCase().indexOf(query) >= 0)

    // Keys written one way, as the backend writes them: modifiers in a
    // fixed order, the key upper-case unless it is a keycode.
    function normalize(keys) {
        const parts = keys.split("+").map(p => p.trim().toUpperCase())
        const mods = ["SUPER", "CTRL", "ALT", "SHIFT"].filter(m => parts.indexOf(m) >= 0 || (m === "CTRL" && parts.indexOf("CONTROL") >= 0))
        let key = parts.filter(p => ["SUPER", "CTRL", "CONTROL", "ALT", "SHIFT"].indexOf(p) < 0).pop() || ""
        if (key.indexOf("CODE:") === 0) key = key.toLowerCase()
        return mods.concat([key]).join(" + ")
    }
    function boundTo(keys) {
        const n = normalize(keys)
        const own = custom.find(b => normalize(b.keys) === n)
        if (own) return own.description
        const hit = (info.bound || []).find(b => b.keys === n)
        return hit ? (hit.description || qsTr("an unnamed shortcut")) : ""
    }
    function actionText(b) {
        if (b.kind === "launch") {
            const app = (backend.lists.apps || []).find(a => a.id + ".desktop" === b.value)
            return qsTr("Opens %1").arg(app ? app.name : b.value)
        }
        if (b.kind === "webapp") return qsTr("Opens %1").arg(b.value)
        return qsTr("Runs %1").arg(b.value)
    }

    // Opened from "Choose Keys…" for Settings: wait for the list of taken
    // keys, so the suggestion is one that is free.
    property bool presetPending: false
    Component.onCompleted: {
        presetPending = !!win.subParams && win.subParams.preset === "settings"
        win.subParams = {}
    }
    onInfoChanged: {
        if (presetPending && info.bound) {
            presetPending = false
            Qt.callLater(() => editor.openFor("settings"))
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: ui.s(8)
        OmaField {
            id: filter
            Layout.fillWidth: true
            placeholderText: qsTr("Filter %n shortcuts", "", page.listed.length)
        }
        OmaButton {
            text: qsTr("Add Shortcut…")
            highlighted: true
            onClicked: editor.openFor("")
        }
    }

    SettingsGroup {
        visible: page.custom.length > 0
        title: qsTr("Your Shortcuts")
        Repeater {
            model: page.custom
            delegate: SettingRow {
                required property var modelData
                label: modelData.description || modelData.value
                sublabel: page.actionText(modelData)
                Text {
                    text: modelData.keys
                    color: ui.accent
                    font.family: ui.font
                    font.pixelSize: ui.small
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                }
                OmaButton {
                    glyph: "\u{F01B4}"
                    bordered: false
                    tooltip: qsTr("Delete")
                    onClicked: backend.act("shortcut.remove", [modelData.keys], ["shortcuts"])
                }
            }
        }
    }

    SettingsGroup {
        visible: page.disabled.length > 0
        title: qsTr("Turned Off")
        Repeater {
            model: page.disabled
            delegate: SettingRow {
                required property string modelData
                label: modelData
                dimmed: true
                OmaButton {
                    text: qsTr("Turn On")
                    onClicked: backend.act("shortcut.enable", [modelData], ["shortcuts"])
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("All Shortcuts")
        footer: qsTr("Turning one off frees its keys for something else. For anything this can't do, edit bindings.lua.")
        Repeater {
            model: page.shown
            delegate: SettingRow {
                id: srow
                required property var modelData
                label: modelData.action
                HoverHandler { id: rowHover }
                Text {
                    text: modelData.keys
                    color: ui.accent
                    font.family: ui.font
                    font.pixelSize: ui.small
                    anchors.verticalCenter: parent.verticalCenter
                    textFormat: Text.PlainText
                }
                OmaButton {
                    text: qsTr("Turn Off")
                    bordered: false
                    opacity: rowHover.hovered || activeFocus ? 1 : 0
                    onClicked: backend.act("shortcut.disable", [modelData.keys], ["shortcuts"])
                }
            }
        }
        ButtonRow {
            label: ""
            text: qsTr("Edit bindings.lua")
            onClicked: backend.act("edit", ["bindings"])
        }
    }

    // ---- New shortcut -------------------------------------------------------
    OmaDialog {
        id: editor
        title: qsTr("New Shortcut")
        acceptText: qsTr("Save")
        canAccept: key.length > 0 && value().length > 0
        width: Math.min(parent ? parent.width - ui.s(48) : 520, Math.round(520 * ui.scale))

        property bool superKey: true
        property bool ctrl: false
        property bool alt: false
        property bool shift: false
        property string key: ""
        property bool recording: false
        property string presetApp: ""
        readonly property string keys: (superKey ? ["SUPER"] : []).concat(ctrl ? ["CTRL"] : [], alt ? ["ALT"] : [],
                                        shift ? ["SHIFT"] : [], key ? [key] : []).join(" + ")
        readonly property string conflict: key ? page.boundTo(keys) : ""
        readonly property var kinds: [
            { value: "launch", text: qsTr("Open an app") },
            { value: "command", text: qsTr("Run a command") },
            { value: "webapp", text: qsTr("Open a web page") }
        ]

        function openFor(preset) {
            superKey = true; ctrl = alt = shift = false; key = ""
            kind.currentIndex = 0; command.text = ""; description.text = ""
            app.currentIndex = -1
            presetApp = ""
            if (preset === "settings") {
                // The first of a few natural choices that is still free.
                const free = ["SUPER + SHIFT + COMMA", "SUPER + CTRL + SHIFT + COMMA", "SUPER + CTRL + S", "SUPER + ALT + S"]
                                 .find(k => !page.boundTo(k)) || "SUPER + SHIFT + COMMA"
                const parts = free.split(" + ")
                superKey = parts.indexOf("SUPER") >= 0; ctrl = parts.indexOf("CTRL") >= 0
                alt = parts.indexOf("ALT") >= 0; shift = parts.indexOf("SHIFT") >= 0; key = parts[parts.length - 1]
                description.text = qsTr("Settings")
                if ((backend.lists.apps || []).some(a => a.id === "omasettings")) {
                    presetApp = "omasettings"
                } else {
                    // Run from a build, Settings has no launcher entry yet.
                    kind.currentIndex = 1
                    command.text = "omasettings"
                }
                app.currentIndex = app.model.findIndex(a => a.id === presetApp)
            }
            open()
            recorder.forceActiveFocus()
        }
        function value() {
            if (kind.currentValue === "launch") return app.currentIndex >= 0 ? app.currentValue + ".desktop" : ""
            return command.text.trim()
        }
        onAccepted: backend.act("shortcut.add", [keys, description.text.trim() || (kind.currentValue === "launch" ? app.currentText : value()),
                                                 kind.currentValue, value()], ["shortcuts"])

        // Press the keys here. A combination Hyprland already uses never
        // reaches the app, so the switches below can set any of them.
        Rectangle {
            id: recorder
            Layout.fillWidth: true
            implicitHeight: ui.s(54)
            radius: theme.radius
            color: activeFocus ? ui.hoverFill : ui.normalFill
            border.width: 1
            border.color: activeFocus ? ui.accent : ui.normalBorder
            activeFocusOnTab: true
            Text {
                anchors.centerIn: parent
                text: editor.key ? editor.keys : recorder.activeFocus ? qsTr("Press the keys now…") : qsTr("Click here and press the keys")
                color: editor.key ? ui.accent : ui.muted
                font.family: ui.font
                font.pixelSize: editor.key ? ui.title : ui.body
                font.bold: !!editor.key
                textFormat: Text.PlainText
            }
            TapHandler { onTapped: recorder.forceActiveFocus() }
            Keys.onPressed: event => {
                const modifierKeys = [Qt.Key_Shift, Qt.Key_Control, Qt.Key_Alt, Qt.Key_Meta, Qt.Key_Super_L, Qt.Key_Super_R, Qt.Key_AltGr]
                if (modifierKeys.indexOf(event.key) >= 0) { event.accepted = true; return }
                if (event.key === Qt.Key_Escape && !event.modifiers) { event.accepted = false; return }
                editor.superKey = !!(event.modifiers & Qt.MetaModifier)
                editor.ctrl = !!(event.modifiers & Qt.ControlModifier)
                editor.alt = !!(event.modifiers & Qt.AltModifier)
                editor.shift = !!(event.modifiers & Qt.ShiftModifier)
                const names = {}
                names[Qt.Key_Return] = "RETURN"; names[Qt.Key_Enter] = "RETURN"; names[Qt.Key_Space] = "SPACE"
                names[Qt.Key_Tab] = "TAB"; names[Qt.Key_Backtab] = "TAB"; names[Qt.Key_Escape] = "ESCAPE"
                names[Qt.Key_Backspace] = "BACKSPACE"; names[Qt.Key_Delete] = "DELETE"; names[Qt.Key_Insert] = "INSERT"
                names[Qt.Key_Home] = "HOME"; names[Qt.Key_End] = "END"; names[Qt.Key_PageUp] = "PAGE_UP"
                names[Qt.Key_PageDown] = "PAGE_DOWN"; names[Qt.Key_Left] = "LEFT"; names[Qt.Key_Right] = "RIGHT"
                names[Qt.Key_Up] = "UP"; names[Qt.Key_Down] = "DOWN"; names[Qt.Key_Print] = "PRINT"
                if (names[event.key]) editor.key = names[event.key]
                else if (event.key >= Qt.Key_A && event.key <= Qt.Key_Z) editor.key = String.fromCharCode(event.key)
                else if (event.key >= Qt.Key_F1 && event.key <= Qt.Key_F24) editor.key = "F" + (event.key - Qt.Key_F1 + 1)
                // Digits and punctuation move around between keyboard
                // layouts; the physical key's code does not.
                else if (event.nativeScanCode > 0) editor.key = "code:" + event.nativeScanCode
                event.accepted = true
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: ui.s(6)
            Repeater {
                model: [{ p: "superKey", t: "Super" }, { p: "ctrl", t: "Ctrl" }, { p: "alt", t: "Alt" }, { p: "shift", t: "Shift" }]
                delegate: OmaButton {
                    required property var modelData
                    text: modelData.t
                    selected: editor[modelData.p]
                    onClicked: editor[modelData.p] = !editor[modelData.p]
                }
            }
            Text {
                text: "+"
                color: ui.muted
                font.family: ui.font
                Layout.leftMargin: ui.s(4)
                Layout.rightMargin: ui.s(4)
            }
            OmaField {
                Layout.fillWidth: true
                placeholderText: qsTr("Key")
                text: editor.key
                onTextEdited: editor.key = text.trim().indexOf("code:") === 0 ? text.trim() : text.trim().toUpperCase()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: editor.conflict.length > 0
            text: qsTr("These keys do “%1” now; saving replaces it.").arg(editor.conflict)
            horizontalAlignment: Text.AlignLeft
            color: ui.urgent
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        Text {
            Layout.fillWidth: true
            visible: !!editor.key && !editor.superKey && !editor.ctrl && !editor.alt
            text: qsTr("Without Super, Ctrl, or Alt, these keys stop working for typing in every app.")
            horizontalAlignment: Text.AlignLeft
            color: ui.urgent
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }

        ChoiceBox {
            id: kind
            Layout.fillWidth: true
            maxWidth: 1000
            model: editor.kinds
            textRole: "text"
            valueRole: "value"
            currentIndex: 0
            Accessible.name: qsTr("What the shortcut does")
        }
        ChoiceBox {
            id: app
            visible: kind.currentValue === "launch"
            Layout.fillWidth: true
            maxWidth: 1000
            model: backend.lists.apps || []
            textRole: "name"
            valueRole: "id"
            currentIndex: -1
            placeholder: qsTr("Choose an app")
            // A ComboBox picks its first item when the list arrives; keep
            // the preset, or nothing.
            onCountChanged: Qt.callLater(() => currentIndex = model.findIndex(a => a.id === editor.presetApp))
            Accessible.name: qsTr("App")
        }
        OmaField {
            id: command
            visible: kind.currentValue !== "launch"
            Layout.fillWidth: true
            placeholderText: kind.currentValue === "webapp" ? "https://…" : qsTr("Command, e.g. omarchy-launch-browser")
        }
        OmaField {
            id: description
            Layout.fillWidth: true
            placeholderText: qsTr("Name it (shown in the shortcut list)")
        }
    }
}
