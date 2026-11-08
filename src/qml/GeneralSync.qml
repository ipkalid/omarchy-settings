import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Sync: this computer's settings, apps, and chosen files kept in a private
// GitHub repository. Changes upload on their own; another computer applies
// them with one click, after seeing what will change. Hardware stays behind.
SettingsPage {
    id: page
    pane: ""
    lists: ["sync"]

    readonly property var sync: backend.lists.sync || ({})
    readonly property bool connected: !!sync.connected
    readonly property bool loaded: backend.lists.sync !== undefined
    readonly property var skip: sync.skip || []
    readonly property var counts: sync.counts || ({})
    readonly property var dotfiles: sync.dotfiles || []
    function busy(action) { return backend.busy.indexOf(action) >= 0 }
    readonly property bool working: busy("sync.connect") || busy("sync.create") || busy("sync.upload") || busy("sync.apply")
    function when(iso) {
        if (!iso) return ""
        return new Date(iso).toLocaleString(Qt.locale(), Locale.ShortFormat)
    }
    readonly property string fromHost: sync.remoteHost || qsTr("another computer")

    // Once connected, look for anything newer as the page opens.
    property bool checked: false
    onConnectedChanged: if (connected && !checked) { checked = true; backend.act("sync.check", [], ["sync"]) }

    // The parts of a snapshot, each of which this computer can leave out.
    readonly property var parts: [
        { key: "settings", title: qsTr("Settings"), glyph: "\u{F0493}",
          about: qsTr("Everything in Settings that isn't hardware, with shortcuts, login items, and window rules") },
        { key: "desktop", title: qsTr("Desktop"), glyph: "\u{F01C4}",
          about: qsTr("The Omarchy menu, dock and bar, apps for file types, branding") },
        { key: "hypr", title: qsTr("Hyprland"), glyph: "\u{F030C}",
          about: qsTr("Keybindings, input, look and feel, autostart, and the rest of ~/.config/hypr, except displays") },
        { key: "apps", title: qsTr("Apps"), glyph: "\u{F003B}",
          about: qsTr("Omarchy's apps, pacman and AUR packages, Flatpak, mise tools, web apps, AppImages") },
        { key: "themes", title: qsTr("Themes & Wallpapers"), glyph: "\u{F03D8}",
          about: qsTr("Themes added by URL, and wallpapers you added") },
        { key: "plugins", title: qsTr("Plugins"), glyph: "\u{F0431}",
          about: qsTr("Shell plugins added by URL, and which are on") },
        { key: "git", title: qsTr("Git Identity"), glyph: "\u{F02A2}",
          about: qsTr("The name and email on your commits") },
        { key: "dotfiles", title: qsTr("Your Files"), glyph: "\u{F0214}",
          about: qsTr("Files and folders you choose below") }
    ]
    function partTitle(key) { const p = parts.find(x => x.key === key); return p ? p.title : key }

    Connections {
        target: backend
        function onActionFinished(action, error) {
            if (action === "sync.connect" || action === "sync.create") page.checked = !error
        }
    }

    // --- Not connected -------------------------------------------------------------

    SettingsGroup {
        visible: page.loaded && !page.connected
        separated: false
        title: qsTr("Keep This Computer in a Repository")
        footer: qsTr("Sync keeps your settings, apps, themes, and chosen files in a private repository on your GitHub account. Changes upload on their own; on another computer, connect the same repository and apply it. Displays, sound devices, networks, power, and printers stay with each computer.")
        ButtonRow {
            visible: !page.sync.signedIn
            label: qsTr("Sign in to GitHub first")
            sublabel: page.sync.gh ? qsTr("Opens a terminal to sign in with the GitHub command")
                                   : qsTr("The GitHub command (gh) isn't installed")
            glyph: "\u{F02A4}"
            tint: ui.urgent
            text: qsTr("Sign In…")
            enabled: !!page.sync.gh
            onClicked: backend.act("launch", ["githubLogin"])
        }
        ButtonRow {
            label: qsTr("Create a private repository")
            sublabel: qsTr("Named omarchy-sync, on your GitHub account")
            glyph: "\u{F0415}"
            text: qsTr("Create")
            highlighted: true
            busy: page.busy("sync.create")
            enabled: !page.working
            onClicked: backend.act("sync.create", [""], ["sync"])
        }
        SettingRow {
            label: qsTr("Or connect one you have")
            glyph: "\u{F0337}"
            OmaField {
                id: repoUrl
                width: Math.round(260 * ui.scale)
                placeholderText: "https://github.com/you/omarchy-sync"
                onAccepted: connectButton.clicked()
            }
            OmaButton {
                id: connectButton
                text: qsTr("Connect")
                spinning: page.busy("sync.connect")
                enabled: repoUrl.text.trim().length > 0 && !page.working
                onClicked: backend.act("sync.connect", [repoUrl.text.trim()], ["sync"])
            }
        }
    }

    // --- Connected -----------------------------------------------------------------

    SettingsGroup {
        visible: page.connected
        separated: false
        title: page.sync.repo || page.sync.url || ""
        SettingRow {
            label: page.busy("sync.apply") ? qsTr("Applying…")
                   : page.busy("sync.upload") ? qsTr("Uploading…")
                   : page.sync.pending ? qsTr("Newer settings from %1").arg(page.fromHost)
                   : qsTr("Up to date")
            sublabel: page.sync.pending && page.sync.remoteUpdated ? page.when(page.sync.remoteUpdated)
                      : page.sync.lastUpload ? qsTr("Last uploaded %1").arg(page.when(page.sync.lastUpload))
                      : page.sync.lastApply ? qsTr("Last applied %1").arg(page.when(page.sync.lastApply)) : ""
            glyph: page.sync.pending ? "\u{F0162}" : "\u{F05E1}"
            tint: page.sync.pending ? ui.accent : ui.fg
            busy: page.working || page.busy("sync.check")
            OmaButton {
                text: page.sync.pending ? qsTr("Review & Apply…") : qsTr("Apply…")
                highlighted: !!page.sync.pending
                bordered: !!page.sync.pending
                enabled: !page.working
                onClicked: applyDialog.open()
            }
        }
        SettingRow {
            visible: !!page.sync.lastError
            label: qsTr("Couldn't sync")
            sublabel: page.sync.lastError || ""
            glyph: "\u{F0026}"
            tint: ui.urgent
            OmaButton {
                text: qsTr("Try Again")
                bordered: false
                enabled: !page.working
                onClicked: backend.act("sync.check", [], ["sync"])
            }
        }
        SettingRow {
            visible: !!page.sync.installing
            label: qsTr("Installing apps in a terminal…")
            sublabel: qsTr("Uploading leaves the app list as it is until they're in")
            glyph: "\u{F01DA}"
            busy: true
        }
        SettingRow {
            visible: !page.sync.installing && !!page.sync.installResult && page.sync.installResult.failed > 0
            label: qsTr("%n apps weren't installed", "", page.sync.installResult ? page.sync.installResult.failed : 0)
            sublabel: page.sync.installResult ? page.sync.installResult.names : ""
            glyph: "\u{F0026}"
            tint: ui.urgent
        }
        SettingRow {
            label: qsTr("Upload changes automatically")
            sublabel: page.sync.pending ? qsTr("Waiting until the newer settings are applied, so they aren't overwritten")
                                        : qsTr("Half a minute after a change, and every 15 minutes")
            glyph: "\u{F0552}"
            OmaToggle {
                checked: page.sync.auto !== false
                onToggled: backend.act("sync.auto", [!checked], ["sync"])
                Accessible.name: qsTr("Upload changes automatically")
            }
        }
        ButtonRow {
            label: page.sync.pending ? qsTr("Replace what's there with this computer's") : qsTr("Upload this computer's settings now")
            text: page.sync.pending ? qsTr("Upload Anyway…") : qsTr("Upload Now")
            busy: page.busy("sync.upload")
            enabled: !page.working
            onClicked: page.sync.pending ? forceDialog.open() : backend.act("sync.upload", [""], ["sync"])
        }
    }

    SettingsGroup {
        id: what
        visible: page.connected
        property bool open: false
        title: qsTr("What Syncs")
        collapsible: true
        collapsed: !open
        onToggle: open = !open
        summary: qsTr("%1 of %2 parts").arg(page.parts.length - page.parts.filter(p => page.skip.indexOf(p.key) >= 0).length).arg(page.parts.length)
        footer: qsTr("A part that's off neither uploads from nor applies to this computer; the repository keeps it for the others.")
        Repeater {
            model: page.parts
            delegate: SettingRow {
                id: partRow
                required property var modelData
                readonly property bool on: page.skip.indexOf(modelData.key) < 0
                label: modelData.title
                sublabel: modelData.about + (page.counts[modelData.key] !== undefined
                          ? " · " + qsTr("%n in the repository", "", page.counts[modelData.key]) : "")
                glyph: modelData.glyph
                dimmed: !on
                OmaToggle {
                    checked: partRow.on
                    onToggled: backend.act("sync.skip", [modelData.key, !checked], ["sync"])
                    Accessible.name: modelData.title
                }
            }
        }
    }

    SettingsGroup {
        visible: page.connected
        title: qsTr("Your Files")
        footer: qsTr("Copied into the repository as they are, from your home folder; on another computer, the old copy is kept beside the new one. Keys and passwords (~/.ssh, ~/.gnupg) can't be added.")
        Repeater {
            model: page.dotfiles
            delegate: SettingRow {
                required property var modelData
                label: "~/" + modelData
                glyph: "\u{F0214}"
                OmaButton {
                    glyph: "\u{F01B4}"
                    bordered: false
                    tooltip: qsTr("Stop syncing")
                    onClicked: backend.act("sync.dotfile.remove", [modelData], ["sync"])
                }
            }
        }
        ButtonRow {
            label: page.dotfiles.length === 0 ? qsTr("Sync config files of your own, like ~/.config/nvim") : ""
            text: qsTr("Add File or Folder…")
            busy: page.busy("sync.dotfile.add")
            onClicked: backend.act("sync.dotfile.add", [], ["sync"])
        }
    }

    SettingsGroup {
        visible: page.connected
        ButtonRow {
            visible: String(page.sync.url || "").indexOf("https://") === 0
            label: qsTr("See the repository and its history")
            text: qsTr("Open on GitHub")
            onClicked: Qt.openUrlExternally(String(page.sync.url).replace(/\.git$/, ""))
        }
        ButtonRow {
            label: qsTr("Stop syncing this computer")
            sublabel: qsTr("The repository stays as it is")
            text: qsTr("Disconnect…")
            enabled: !page.working
            onClicked: disconnectDialog.open()
        }
    }

    // --- Dialogs -------------------------------------------------------------------

    OmaDialog {
        id: applyDialog
        objectName: "syncApplyDialog"
        readonly property var preview: backend.lists.syncPreview || ({})
        readonly property bool loading: page.busy("list:syncPreview")
        readonly property var groups: preview.groups || []
        width: Math.min(parent ? parent.width - ui.s(48) : 560, Math.round(560 * ui.scale))
        title: preview.host ? qsTr("Apply from %1?").arg(preview.host) : qsTr("Apply from the repository?")
        acceptText: qsTr("Apply")
        canAccept: !loading && !preview.error && groups.length > 0
        onOpened: backend.load("syncPreview")
        onAccepted: backend.act("sync.apply", [], ["sync", "software", "webapps", "themes", "plugins", "menu"])

        RowLayout {
            visible: applyDialog.loading
            spacing: ui.s(8)
            Spinner {}
            Text {
                text: qsTr("Looking at what's different…")
                color: ui.muted
                font.family: ui.font
                font.pixelSize: ui.small
                textFormat: Text.PlainText
            }
        }
        Text {
            visible: !applyDialog.loading
            Layout.fillWidth: true
            text: applyDialog.preview.error ? applyDialog.preview.error
                  : applyDialog.groups.length === 0 ? qsTr("This computer already matches the repository.")
                  : qsTr("%n changes. Apps are only added, never removed; the ones that need a password install in a terminal.", "", applyDialog.preview.total || 0)
            horizontalAlignment: Text.AlignLeft
            color: applyDialog.preview.error ? ui.urgent : ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        Flickable {
            visible: !applyDialog.loading && applyDialog.groups.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(changes.implicitHeight, (win.height || 600) * 0.5)
            contentHeight: changes.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            ColumnLayout {
                id: changes
                width: parent.width
                spacing: ui.s(10)
                Repeater {
                    model: applyDialog.groups
                    delegate: SettingsGroup {
                        id: changeGroup
                        required property var modelData
                        required property int index
                        property bool open: false
                        separated: index > 0
                        title: page.partTitle(modelData.key)
                        collapsible: true
                        collapsed: !open
                        onToggle: open = !open
                        summary: qsTr("%n changes", "", modelData.items.length)
                        Repeater {
                            model: changeGroup.open ? changeGroup.modelData.items : []
                            delegate: SettingRow {
                                required property var modelData
                                width: parent.width
                                label: modelData.setting ? qsTranslate("Setting", modelData.label) : modelData.label
                                sublabel: modelData.detail || ""
                            }
                        }
                    }
                }
            }
        }
    }

    OmaDialog {
        id: forceDialog
        title: qsTr("Upload over the newer settings?")
        acceptText: qsTr("Upload Anyway")
        onAccepted: backend.act("sync.upload", ["force"], ["sync"])
        Text {
            Layout.fillWidth: true
            text: qsTr("The repository has settings from %1 that this computer hasn't applied. Uploading replaces them with this computer's; they stay in the repository's history.").arg(page.fromHost)
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }

    OmaDialog {
        id: disconnectDialog
        title: qsTr("Stop syncing this computer?")
        acceptText: qsTr("Disconnect")
        onAccepted: backend.act("sync.disconnect", [], ["sync"])
        Text {
            Layout.fillWidth: true
            text: qsTr("Nothing uploads or applies from here any more. The repository and everything in it stay on GitHub, and you can connect again later.")
            horizontalAlignment: Text.AlignLeft
            color: ui.muted
            font.family: ui.font
            font.pixelSize: ui.small
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
    }
}
