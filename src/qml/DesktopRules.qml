import QtQuick
import QtQuick.Layouts

// Rules for how particular apps' windows open, written through Omarchy's
// o.window helper into the generated Hyprland file. Omarchy's own rules
// (Steam floats, the file pickers center) are listed too: they live in
// Omarchy's files, so changing one saves a rule of the user's that comes
// later in the config and wins where the two differ.
SettingsPage {
    id: page
    pane: ""
    lists: ["windowRules", "apps"]

    readonly property var info: backend.lists.windowRules || ({})
    readonly property var rules: info.rules || []
    readonly property var others: info.others || []
    readonly property var windows: info.windows || []

    // The rule the form shows: one of the user's (its index), or a new one,
    // maybe started from one of Omarchy's.
    property int editing: -1
    property var basedOn: null
    property bool othersOpen: false
    // Set for the moment a row's Delete button is pressed, so the tap
    // doesn't also open the rule it removes.
    property bool ignoreTap: false

    // The keys the form sets; anything else a rule has is kept as it is.
    readonly property var formKeys: ["class", "title", "float", "tile", "center", "workspace", "width", "height",
                                     "opacity", "pin", "fullscreen"]

    // ---- How a rule reads ---------------------------------------------------

    // A class or title pattern as the text someone would type: ^(mpv)$ and
    // steam read as mpv and steam; a real pattern stays as written.
    function plainText(pattern) {
        if (!pattern) return ""
        // Anchors and one pair of brackets around it all add nothing.
        let inner = pattern.replace(/^\^/, "").replace(/([^\\])\$$/, "$1")
        const wrapped = /^\((.*)\)$/.exec(inner)
        if (wrapped) inner = wrapped[1]
        // Plain when nothing but escaped characters is special in it.
        return /[.*+?^${}()|[\]\\]/.test(inner.replace(/\\./g, "")) ? "" : inner.replace(/\\(.)/g, "$1")
    }
    function patternFor(text) { return "^(" + text.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + ")$" }
    // A pattern for an app that isn't installed, read as a name: the first
    // of its choices, with letter-case brackets, wildcards and optional
    // parts taken out (^([bB]itwarden)$ reads Bitwarden). A pattern that
    // doesn't come out as plain text stays as written.
    function readable(pattern) {
        let s = pattern.replace(/^\^/, "").replace(/([^\\])\$$/, "$1")
        const wrapped = /^\((.*)\)$/.exec(s)
        if (wrapped && wrapped[1].indexOf("(") < 0) s = wrapped[1]
        s = s.split("|")[0]
        s = s.replace(/\[(.)\|?(.)\]/g, (m, a, b) => b === b.toUpperCase() ? b : a)
        s = s.replace(/\([^()]*\)\?/g, "").replace(/\.[*+?]/g, " ").replace(/\\?.\?/g, "")
        s = s.replace(/\\(.)/g, "$1").replace(/\s+/g, " ").trim().replace(/[\s\-–:_]+$/, "")
        return !s || /[()[\]|*+?{}^$]/.test(s) ? "" : s
    }
    function shown(pattern) { return pattern ? plainText(pattern) || readable(pattern) || pattern : "" }

    function appLabel(r) {
        let app = r.appName || ""
        if (!app && r.appNames)
            app = r.appNames.length <= 3 ? r.appNames.join(", ")
                : qsTr("%1 and %n more", "", r.appNames.length - 2).arg(r.appNames.slice(0, 2).join(", "))
        if (!app) app = shown(r.class) || shown(r.title)
        return r.class && r.title ? qsTr("%1, “%2” window").arg(app).arg(shown(r.title)) : app
    }

    function size(rule) {
        if (rule.width && rule.height) return [rule.width, rule.height]
        const s = rule.size
        return s && typeof s[0] === "number" && typeof s[1] === "number" ? s : null
    }

    function describe(rule) {
        const parts = []
        const known = ["class", "title", "appName", "appNames", "appIcon", "width", "height", "size", "tag"]
        if (rule.float) parts.push(qsTr("floating"))
        if (rule.tile) parts.push(qsTr("tiled"))
        if (rule.center) parts.push(qsTr("centered"))
        if (rule.workspace) parts.push(qsTr("on workspace %1").arg(rule.workspace))
        const wh = size(rule)
        if (wh) parts.push(qsTr("%1 × %2").arg(wh[0]).arg(wh[1]))
        if (rule.opacity) parts.push(qsTr("%1% opaque").arg(Math.round(rule.opacity * 100)))
        if (rule.transparent) parts.push(qsTr("transparent"))
        if (rule.pin) parts.push(qsTr("on every workspace"))
        if (rule.fullscreen) parts.push(qsTr("full screen"))
        if (rule.maximize) parts.push(qsTr("maximized"))
        if (rule.idle_inhibit) parts.push(qsTr("keeps the screen awake"))
        if (rule.no_screen_share) parts.push(qsTr("hidden from screen sharing"))
        const said = ["float", "tile", "center", "workspace", "opacity", "transparent", "pin", "fullscreen", "maximize",
                      "idle_inhibit", "no_screen_share"]
        const rest = Object.keys(rule).filter(k => known.indexOf(k) < 0 && said.indexOf(k) < 0).length
        if (rest > 0) parts.push(qsTr("%n other settings", "", rest))
        return parts.join(", ")
    }

    // ---- The form -------------------------------------------------------------

    function clearForm() {
        editing = -1
        basedOn = null
        appPicker.currentIndex = -1
        appClass.text = ""
        appClass.pattern = ""
        windowTitle.text = ""
        windowTitle.pattern = ""
        layout.currentIndex = 0
        centered.checked = pinned.checked = fullscreen.checked = false
        workspace.currentIndex = 0
        sizeWidth.text = sizeHeight.text = ""
        opacitySlider.value = 1
    }

    // Fills the form from a rule's settings: the user's own, or the rules
    // object of one of Omarchy's.
    function fill(cls, title, rule) {
        appPicker.currentIndex = -1
        cls = cls || ""
        title = title || ""
        appClass.pattern = plainText(cls) ? "" : cls
        appClass.text = plainText(cls) || cls
        windowTitle.pattern = plainText(title) ? "" : title
        windowTitle.text = plainText(title) || title
        layout.currentIndex = rule.float ? 1 : rule.tile ? 2 : 0
        centered.checked = !!rule.center
        pinned.checked = !!rule.pin
        fullscreen.checked = !!rule.fullscreen
        const ws = Number(rule.workspace)
        workspace.currentIndex = ws >= 1 && ws <= 10 && String(ws) === String(rule.workspace) ? ws : 0
        const wh = size(rule)
        sizeWidth.text = wh ? String(wh[0]) : ""
        sizeHeight.text = wh ? String(wh[1]) : ""
        opacitySlider.value = rule.opacity ? rule.opacity : 1
        Qt.callLater(() => page.reveal(formGroup))
    }

    function edit(index) {
        const r = rules[index]
        if (!r) return
        clearForm()
        editing = index
        fill(r.class, r.title, r)
    }

    function override(other) {
        clearForm()
        basedOn = other
        fill(other.class, other.title, other.rules)
    }

    // The pattern a field stands for: the one it was filled with, while its
    // text is unchanged, or else the typed text matched exactly.
    function patternOf(field) {
        const text = field.text.trim()
        if (!text) return ""
        return field.pattern && text === field.pattern ? field.pattern : patternFor(text)
    }

    readonly property bool formHasEffect: layout.currentIndex > 0 || centered.checked || workspace.currentIndex > 0
        || (sizeWidth.text && sizeHeight.text) || opacitySlider.value < 1 || pinned.checked || fullscreen.checked
        || (editing >= 0 && Object.keys(rules[editing] || {}).some(k => formKeys.indexOf(k) < 0
                                                                        && ["appName", "appNames", "appIcon"].indexOf(k) < 0))

    function save() {
        const rule = {}
        // Keep what the form doesn't show (a rule set by hand or the CLI).
        if (editing >= 0)
            for (const k in rules[editing])
                if (formKeys.indexOf(k) < 0 && ["appName", "appNames", "appIcon"].indexOf(k) < 0)
                    rule[k] = rules[editing][k]
        const cls = patternOf(appClass), title = patternOf(windowTitle)
        if (cls) rule.class = cls
        if (title) rule.title = title
        if (layout.currentIndex === 1) rule.float = true
        if (layout.currentIndex === 2) rule.tile = true
        if (centered.checked) rule.center = true
        if (workspace.currentIndex > 0) rule.workspace = workspace.currentText
        if (sizeWidth.text && sizeHeight.text) { rule.width = Number(sizeWidth.text); rule.height = Number(sizeHeight.text) }
        if (opacitySlider.value < 1) rule.opacity = opacitySlider.value
        if (pinned.checked) rule.pin = true
        if (fullscreen.checked) rule.fullscreen = true
        if (editing >= 0)
            backend.act("rule.update", [editing, rule], ["windowRules"])
        else
            backend.act("rule.add", [rule], ["windowRules"])
        clearForm()
    }

    // Apps to pick from: what has a window open now, then everything
    // installed, by the class its windows carry.
    readonly property var appChoices: {
        const out = [], seen = {}
        for (const w of windows)
            if (!seen[w.class]) { seen[w.class] = true; out.push({ text: qsTr("%1 · open").arg(w.class), value: w.class }) }
        for (const a of (backend.lists.apps || []))
            if (a.wmClass && !seen[a.wmClass]) { seen[a.wmClass] = true; out.push({ text: a.name, value: a.wmClass }) }
        return out
    }

    // ---- The page -------------------------------------------------------------

    SettingsGroup {
        separated: false
        title: qsTr("Rules")
        footer: page.rules.length === 0 ? qsTr("No rules yet. Rules apply to windows as they open.")
                                        : qsTr("Open a rule to change it. Rules apply to windows as they open; close and reopen an app to see a change.")
        Repeater {
            model: page.rules
            delegate: SettingRow {
                required property var modelData
                required property int index
                label: page.appLabel(modelData)
                sublabel: page.describe(modelData)
                glyph: "\u{F05B2}"
                clickable: true
                selected: page.editing === index
                onActivated: if (!page.ignoreTap) page.edit(index)
                OmaButton {
                    glyph: "\u{F01B4}"
                    bordered: false
                    tooltip: qsTr("Delete")
                    onClicked: {
                        page.ignoreTap = true
                        Qt.callLater(() => page.ignoreTap = false)
                        if (page.editing === index) page.clearForm()
                        else if (page.editing > index) page.editing--
                        backend.act("rule.remove", [index], ["windowRules"])
                    }
                }
            }
        }
    }

    SettingsGroup {
        id: formGroup
        title: page.editing >= 0 ? qsTr("Edit Rule") : qsTr("New Rule")
        footer: qsTr("Pick an app, or type its class. Find any window's class with: hyprctl clients")

        SettingRow {
            visible: !!page.basedOn
            label: page.basedOn ? qsTr("Starting from %1").arg(page.basedOn.file) : ""
            sublabel: page.basedOn ? page.describe(page.basedOn.rules) : ""
            glyph: "\u{F02FC}"
        }
        SettingRow {
            label: qsTr("App")
            ChoiceBox {
                id: appPicker
                maxWidth: Math.round(220 * ui.scale)
                model: page.appChoices
                textRole: "text"
                valueRole: "value"
                currentIndex: -1
                placeholder: qsTr("Choose an app")
                onActivated: { appClass.pattern = ""; appClass.text = currentValue }
                Accessible.name: qsTr("App")
            }
            OmaField {
                id: appClass
                // The pattern the field was filled with, kept while unchanged.
                property string pattern: ""
                width: Math.round(200 * ui.scale)
                placeholderText: qsTr("App class")
            }
        }
        SettingRow {
            label: qsTr("Window title")
            sublabel: qsTr("Leave empty for all of the app's windows")
            OmaField {
                id: windowTitle
                property string pattern: ""
                width: Math.round(200 * ui.scale)
                placeholderText: qsTr("Any")
            }
        }
        SettingRow {
            label: qsTr("Open as")
            ChoiceBox {
                id: layout
                maxWidth: Math.round(160 * ui.scale)
                model: [qsTr("Default"), qsTr("Floating"), qsTr("Tiled")]
                currentIndex: 0
                Accessible.name: qsTr("Open as")
            }
        }
        SettingRow {
            label: qsTr("Center it")
            OmaToggle { id: centered; onToggled: checked = !checked }
        }
        SettingRow {
            label: qsTr("On workspace")
            ChoiceBox {
                id: workspace
                maxWidth: Math.round(160 * ui.scale)
                model: [qsTr("Any")].concat([1, 2, 3, 4, 5, 6, 7, 8, 9, 10].map(n => String(n)))
                currentIndex: 0
                Accessible.name: qsTr("Workspace")
            }
        }
        SettingRow {
            label: qsTr("Size")
            sublabel: qsTr("For floating windows; leave empty to let the app decide")
            OmaField { id: sizeWidth; width: Math.round(90 * ui.scale); placeholderText: qsTr("Width"); validator: IntValidator { bottom: 100; top: 10000 } }
            OmaField { id: sizeHeight; width: Math.round(90 * ui.scale); placeholderText: qsTr("Height"); validator: IntValidator { bottom: 100; top: 10000 } }
        }
        SettingRow {
            label: qsTr("Opacity")
            OmaSlider {
                id: opacitySlider
                width: Math.round(200 * ui.scale)
                from: 0.3; to: 1; stepSize: 0.05; value: 1
            }
            Text {
                text: qsTr("%1%").arg(Math.round(opacitySlider.value * 100))
                color: ui.fg
                font.family: ui.font
                font.pixelSize: ui.small
                anchors.verticalCenter: parent.verticalCenter
                textFormat: Text.PlainText
            }
        }
        SettingRow {
            label: qsTr("Show on every workspace")
            OmaToggle { id: pinned; onToggled: checked = !checked }
        }
        SettingRow {
            label: qsTr("Open it full screen")
            OmaToggle { id: fullscreen; onToggled: checked = !checked }
        }
        SettingRow {
            label: ""
            OmaButton {
                visible: page.editing >= 0 || !!page.basedOn
                text: qsTr("Cancel")
                onClicked: page.clearForm()
            }
            OmaButton {
                text: page.editing >= 0 ? qsTr("Save") : qsTr("Add Rule")
                highlighted: true
                enabled: (appClass.text.trim().length > 0 || windowTitle.text.trim().length > 0) && page.formHasEffect
                onClicked: page.save()
            }
        }
    }

    SettingsGroup {
        visible: page.others.length > 0
        title: qsTr("Other Rules")
        footer: qsTr("Set by Omarchy or your own Hyprland files. Open one to change it: Settings saves the change as a rule of yours, which wins where the two differ.")
        collapsible: true
        collapsed: !page.othersOpen
        summary: qsTr("%n rules", "", page.others.length)
        onToggle: page.othersOpen = !page.othersOpen
        Repeater {
            model: page.others
            delegate: SettingRow {
                required property var modelData
                label: page.appLabel(modelData)
                sublabel: page.describe(modelData.rules) + " · " + modelData.file
                glyph: "\u{F05B2}"
                clickable: true
                selected: page.basedOn === modelData
                onActivated: page.override(modelData)
            }
        }
    }
}
