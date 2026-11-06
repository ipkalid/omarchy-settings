import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import "qml"

ApplicationWindow {
    id: win
    width: 1000
    height: 720
    minimumWidth: 760
    minimumHeight: 520
    visible: true
    title: qsTr("Settings")
    color: ui.bg
    font.family: ui.font
    font.pixelSize: ui.body

    Material.theme: theme.dark ? Material.Dark : Material.Light
    Material.accent: theme.accent
    Material.background: ui.bg
    Material.foreground: ui.fg
    Material.roundedScale: theme.radius

    // Colors, type, and spacing, taken from the Omarchy shell's own design
    // tokens (shell/Commons/Style.qml) and the current theme, so the window
    // looks like the shell's Wi-Fi and Bluetooth panels.
    QtObject {
        id: ui
        readonly property var c: theme.colors
        readonly property color fg: c.foreground || (theme.dark ? "#cacccc" : "#2e3440")
        readonly property color bg: c.background || (theme.dark ? "#101315" : "#eff1f5")
        readonly property color accent: theme.accent
        readonly property color urgent: c.red || "#a55555"
        readonly property color muted: Qt.darker(fg, theme.dark ? 1.4 : 0.7)
        readonly property color muted2: Qt.darker(fg, theme.dark ? 1.5 : 0.65)
        readonly property color normalFill: alpha(fg, 0.04)
        readonly property color hoverFill: alpha(fg, 0.08)
        readonly property color selectedFill: alpha(fg, 0.18)
        readonly property color pressedFill: alpha(fg, 0.22)
        readonly property color normalBorder: alpha(fg, 0.4)
        readonly property color separator: alpha(fg, 0.12)
        function alpha(color, a) { return Qt.rgba(color.r, color.g, color.b, a) }

        readonly property string font: theme.fontFamily
        readonly property string iconFont: "JetBrainsMono Nerd Font"
        // The shell's type scale, anchored at its 12px base and following
        // the text size picked in Omarchy.
        readonly property real scale: theme.textSize / 12
        function s(px) { return Math.round(px * scale) }
        readonly property int caption: Math.round(theme.textSize * 0.833)
        readonly property int small: Math.round(theme.textSize * 0.917)
        readonly property int body: theme.textSize
        readonly property int title: Math.round(theme.textSize * 1.167)
        readonly property int heading: Math.round(theme.textSize * 1.333)
        readonly property int display: theme.textSize * 2
        readonly property int icon: title
        readonly property int controlHeight: s(28)
        readonly property int rowHeight: s(36)
    }

    readonly property var caps: backend.lists.capabilities || ({})

    // The sidebar, grouped the way macOS System Settings groups it.
    readonly property var panes: [
        { id: "wifi", group: 0, title: qsTr("Wi-Fi"), glyph: "\u{F05A9}", show: caps.wifi !== false,
          keywords: qsTr("network internet wireless password") },
        { id: "bluetooth", group: 0, title: qsTr("Bluetooth"), glyph: "\u{F00AF}", show: caps.bluetooth !== false,
          keywords: qsTr("headphones speaker mouse pair devices") },
        { id: "vpn", group: 0, title: qsTr("VPN"), glyph: "\u{F0582}", show: true,
          keywords: qsTr("vpn wireguard openvpn private network tunnel") },
        { id: "power", group: 0, title: caps.battery ? qsTr("Battery") : qsTr("Power"), glyph: caps.battery ? "\u{F0079}" : "\u{F0241}",
          show: true, keywords: qsTr("battery power profile performance energy suspend") },
        { id: "general", group: 1, title: qsTr("General"), glyph: "\u{F0493}", show: true,
          keywords: qsTr("about update language region date time zone clock default apps browser terminal editor name login items startup web apps file types backup export import") },
        { id: "storage", group: 1, title: qsTr("Storage"), glyph: "\u{F02CA}", show: true,
          keywords: qsTr("storage disk space free full folders snapshots snapper package cache") },
        { id: "software", group: 1, title: qsTr("Apps"), glyph: "\u{F003B}", show: true,
          keywords: qsTr("apps install remove uninstall software packages aur browser editor terminal gaming steam development ai dictation") },
        { id: "appearance", group: 1, title: qsTr("Appearance"), glyph: "\u{F03D8}", show: true,
          keywords: qsTr("theme dark light color font text size corners transparency animations") },
        { id: "fonts", group: 1, title: qsTr("Fonts"), glyph: "\u{F06D6}", show: true,
          keywords: qsTr("font fonts monospace text size install cjk chinese japanese korean") },
        { id: "desktop", group: 1, title: qsTr("Desktop & Dock"), glyph: "\u{F056E}", show: true,
          keywords: qsTr("dock windows gaps layout tiling focus pointer") },
        { id: "bar", group: 1, title: qsTr("Bar"), glyph: "\u{F06EB}", show: true,
          keywords: qsTr("bar top panel widgets arrange order clock tray position") },
        { id: "menu", group: 1, title: qsTr("Omarchy Menu"), glyph: "\u{F035C}", show: true,
          keywords: qsTr("omarchy menu hide items sections launcher entries") },
        { id: "plugins", group: 1, title: qsTr("Plugins"), glyph: "\u{F0431}", show: true,
          keywords: qsTr("plugins extensions widgets enable disable update install") },
        { id: "displays", group: 1, title: qsTr("Displays"), glyph: "\u{F0379}", show: true,
          keywords: qsTr("monitor resolution refresh rate scale night shift brightness rotation") },
        { id: "wallpaper", group: 1, title: qsTr("Wallpaper"), glyph: "\u{F02E9}", show: true,
          keywords: qsTr("background desktop picture image") },
        { id: "notifications", group: 2, title: qsTr("Notifications"), glyph: "\u{F009A}", show: true,
          keywords: qsTr("do not disturb focus alerts") },
        { id: "sound", group: 2, title: qsTr("Sound"), glyph: "\u{F057E}", show: true,
          keywords: qsTr("volume output input microphone speakers headphones mute") },
        { id: "lock", group: 3, title: qsTr("Lock Screen"), glyph: "\u{F033E}", show: true,
          keywords: qsTr("idle screensaver lock sleep awake") },
        { id: "security", group: 3, title: qsTr("Login & Security"), glyph: "\u{F0237}", show: true,
          keywords: qsTr("password fingerprint security key ssh encryption") },
        { id: "firewall", group: 3, title: qsTr("Firewall"), glyph: "\u{F0498}", show: true,
          keywords: qsTr("firewall ufw ports block allow incoming security") },
        { id: "keyboard", group: 4, title: qsTr("Keyboard"), glyph: "\u{F030C}", show: true,
          keywords: qsTr("layout input sources repeat caps lock compose shortcuts keybindings") },
        { id: "pointer", group: 4, title: caps.touchpad ? qsTr("Mouse & Trackpad") : qsTr("Mouse"), glyph: "\u{F037D}",
          show: true, keywords: qsTr("mouse trackpad touchpad scroll speed tap click gestures") },
        { id: "printers", group: 4, title: qsTr("Printers"), glyph: "\u{F042A}", show: true,
          keywords: qsTr("printer print cups test page scanner") },
    ]

    function paneById(id) { return panes.find(p => p.id === id) }

    // Labels of every setting by pane, in the current language, so searching
    // for "repeat" or "night" finds the pane that has it.
    readonly property var settingLabels: {
        const _ = translator.language
        const byPane = {}
        for (const s of backend.index)
            byPane[s.pane] = (byPane[s.pane] || "") + " " + qsTranslate("Setting", s.label) + " " + s.label
        return byPane
    }

    property string query: ""
    readonly property var visiblePanes: {
        const q = query.trim().toLowerCase()
        return panes.filter(p => p.show && (q.length === 0 ||
            (p.title + " " + p.keywords + " " + (settingLabels[p.id] || "")).toLowerCase().indexOf(q) >= 0))
    }

    // Where we are: a pane, and optionally a page inside it (General > About).
    property string pane: "wifi"
    property string sub: ""
    property string subTitle: ""
    // What a sub-page was opened for (Settings > Choose Keys opens Shortcuts
    // ready to add one for Settings).
    property var subParams: ({})
    property var backStack: []
    property var forwardStack: []

    function go(paneId, subPage, subPageTitle, record) {
        if (paneId === pane && (subPage || "") === sub)
            return
        if (record !== false) {
            backStack = backStack.concat([{ pane: pane, sub: sub, subTitle: subTitle }])
            forwardStack = []
        }
        pane = paneId
        sub = subPage || ""
        subTitle = subPageTitle || ""
    }
    // The first row matching the search on a page is scrolled to once, not
    // again every time something re-renders.
    property string revealedFor: ""
    function revealRow(row) {
        const key = pane + "|" + sub + "|" + query
        if (key === revealedFor || !row.visible || !pageLoader.item || !pageLoader.item.reveal)
            return
        revealedFor = key
        pageLoader.item.reveal(row)
    }

    function openSub(page, pageTitle, params) { subParams = params || {}; go(pane, page, pageTitle) }
    function back() {
        if (backStack.length === 0) return
        const to = backStack[backStack.length - 1]
        forwardStack = forwardStack.concat([{ pane: pane, sub: sub, subTitle: subTitle }])
        backStack = backStack.slice(0, -1)
        go(to.pane, to.sub, to.subTitle, false)
    }
    function forward() {
        if (forwardStack.length === 0) return
        const to = forwardStack[forwardStack.length - 1]
        backStack = backStack.concat([{ pane: pane, sub: sub, subTitle: subTitle }])
        forwardStack = forwardStack.slice(0, -1)
        go(to.pane, to.sub, to.subTitle, false)
    }

    Component.onCompleted: {
        backend.load("capabilities")
        if (initialPane && paneById(initialPane))
            pane = initialPane
    }

    property bool helpVisible: false

    function typing() {
        const item = win.activeFocusItem
        return item && (item instanceof TextInput || item instanceof TextEdit) && !item.readOnly
    }

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequences: ["Q", StandardKey.Quit]; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequences: [StandardKey.Find, "/"]; context: Qt.ApplicationShortcut; onActivated: { search.forceActiveFocus(); search.selectAll() } }
    Shortcut { sequences: [StandardKey.Undo]; context: Qt.ApplicationShortcut; enabled: !win.typing(); onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo, "Ctrl+Shift+Z"]; context: Qt.ApplicationShortcut; enabled: !win.typing(); onActivated: backend.redo() }
    Shortcut { sequences: ["Alt+Left", "Ctrl+["]; context: Qt.ApplicationShortcut; onActivated: win.back() }
    Shortcut { sequences: ["Alt+Right", "Ctrl+]"]; context: Qt.ApplicationShortcut; onActivated: win.forward() }
    Shortcut { sequences: ["Ctrl+R", "F5"]; context: Qt.ApplicationShortcut; onActivated: { if (pageLoader.item) pageLoader.item.reload(); backend.load("capabilities") } }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible) win.helpVisible = false
            else if (win.query.length > 0) { search.text = ""; sidebar.forceActiveFocus() }
            else if (win.sub.length > 0) win.back()
        }
    }
    Shortcut { sequence: "Ctrl+Down"; context: Qt.ApplicationShortcut; onActivated: sidebar.step(1) }
    Shortcut { sequence: "Ctrl+Up"; context: Qt.ApplicationShortcut; onActivated: sidebar.step(-1) }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // ---- Sidebar ------------------------------------------------------
            Rectangle {
                Layout.fillHeight: true
                Layout.preferredWidth: ui.s(220)
                color: ui.bg

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: ui.s(12)
                    anchors.topMargin: ui.s(14)
                    spacing: ui.s(10)

                    Item {
                        Layout.fillWidth: true
                        implicitHeight: search.implicitHeight
                        OmaField {
                            id: search
                            anchors.left: parent.left
                            anchors.right: parent.right
                            // Room for the search glyph on both sides, so it
                            // clears the text in either reading direction.
                            leftPadding: ui.s(18) + searchGlyph.width
                            rightPadding: ui.s(18) + searchGlyph.width
                            placeholderText: qsTr("Search")
                            onTextChanged: win.query = text
                            Keys.onDownPressed: sidebar.forceActiveFocus()
                            Keys.onReturnPressed: {
                                if (win.visiblePanes.length > 0) {
                                    win.go(win.visiblePanes[0].id)
                                    sidebar.forceActiveFocus()
                                }
                            }
                            Accessible.name: qsTr("Search settings")
                        }
                        PaneIcon {
                            id: searchGlyph
                            glyph: "\u{F0349}"
                            tint: ui.muted
                            size: ui.body
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: ui.s(10)
                        }
                    }

                    ListView {
                        id: sidebar
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        focus: true
                        model: win.visiblePanes
                        currentIndex: win.visiblePanes.findIndex(p => p.id === win.pane)
                        keyNavigationEnabled: false
                        boundsBehavior: Flickable.StopAtBounds

                        function step(delta) {
                            const n = win.visiblePanes.length
                            if (n === 0) return
                            const i = Math.max(0, Math.min(n - 1, (currentIndex < 0 ? 0 : currentIndex + delta)))
                            win.go(win.visiblePanes[i].id)
                        }
                        Keys.onDownPressed: step(1)
                        Keys.onUpPressed: step(-1)
                        Keys.onRightPressed: if (pageLoader.item) pageLoader.item.nextItemInFocusChain(true).forceActiveFocus()

                        delegate: Item {
                            id: item
                            required property var modelData
                            required property int index
                            readonly property bool selected: modelData.id === win.pane
                            // A hairline between groups, as in the shell's menus.
                            readonly property bool gap: index > 0 && win.visiblePanes[index - 1].group !== modelData.group
                            width: ListView.view.width
                            height: ui.s(30) + (gap ? ui.s(13) : 0)

                            Rectangle {
                                visible: item.gap
                                anchors.left: parent.left
                                anchors.right: parent.right
                                y: ui.s(6)
                                height: 1
                                color: ui.separator
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: ui.s(30)
                                radius: theme.radius
                                color: item.selected ? ui.hoverFill : hover.hovered ? ui.normalFill : "transparent"
                                border.width: item.selected && sidebar.activeFocus ? 1 : 0
                                border.color: ui.alpha(ui.fg, 0.25)

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: ui.s(10)
                                    anchors.rightMargin: ui.s(10)
                                    spacing: ui.s(10)
                                    PaneIcon {
                                        glyph: item.modelData.glyph
                                        tint: item.selected ? ui.accent : ui.fg
                                        Layout.preferredWidth: ui.s(18)
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: item.modelData.title
                                        horizontalAlignment: Text.AlignLeft
                                        color: item.selected ? ui.accent : ui.fg
                                        font.family: ui.font
                                        font.pixelSize: ui.body
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                    }
                                }
                                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: { win.go(item.modelData.id); sidebar.forceActiveFocus() } }
                            }
                        }
                    }

                    Text {
                        visible: win.visiblePanes.length === 0
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: qsTr("No results for “%1”").arg(win.query)
                        color: ui.muted
                        font.family: ui.font
                        font.pixelSize: ui.small
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: ui.separator
                }
            }

            // ---- Detail -------------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Back, forward, and where you are.
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: ui.s(48)
                    Layout.leftMargin: ui.s(12)
                    Layout.rightMargin: ui.s(12)
                    spacing: ui.s(4)

                    OmaButton {
                        glyph: Qt.application.layoutDirection === Qt.RightToLeft ? "\u{F0142}" : "\u{F0141}"
                        bordered: false
                        enabled: win.backStack.length > 0
                        onClicked: win.back()
                        tooltip: qsTr("Back")
                    }
                    OmaButton {
                        glyph: Qt.application.layoutDirection === Qt.RightToLeft ? "\u{F0141}" : "\u{F0142}"
                        bordered: false
                        enabled: win.forwardStack.length > 0
                        onClicked: win.forward()
                        tooltip: qsTr("Forward")
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: ui.s(6)
                        text: win.sub.length > 0 ? win.subTitle : (win.paneById(win.pane) || {}).title || ""
                        horizontalAlignment: Text.AlignLeft
                        color: ui.fg
                        font.family: ui.font
                        font.pixelSize: ui.body
                        font.bold: true
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    OmaButton {
                        glyph: "\u{F054C}"
                        bordered: false
                        visible: backend.canUndo
                        onClicked: backend.undo()
                        tooltip: qsTr("Undo the last change (Ctrl+Z)")
                    }
                    OmaButton {
                        text: "?"
                        bordered: false
                        onClicked: win.helpVisible = true
                        tooltip: qsTr("Keyboard shortcuts (?)")
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: ui.separator }

                Loader {
                    id: pageLoader
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    source: {
                        const page = win.sub.length > 0 ? win.sub
                                   : "Pane" + win.pane.charAt(0).toUpperCase() + win.pane.slice(1)
                        return "qrc:/qml/" + page + ".qml"
                    }
                }

                // Errors are said in the window, never swallowed.
                Rectangle {
                    id: banner
                    Layout.fillWidth: true
                    Layout.margins: ui.s(12)
                    visible: backend.message.length > 0
                    implicitHeight: bannerRow.implicitHeight + ui.s(16)
                    radius: theme.radius
                    color: ui.alpha(ui.urgent, 0.12)
                    border.color: ui.urgent
                    border.width: 1

                    RowLayout {
                        id: bannerRow
                        anchors.fill: parent
                        anchors.margins: ui.s(8)
                        anchors.leftMargin: ui.s(12)
                        spacing: ui.s(10)
                        PaneIcon { glyph: "\u{F0026}"; tint: ui.urgent }
                        Text {
                            Layout.fillWidth: true
                            text: backend.message
                            horizontalAlignment: Text.AlignLeft
                            color: ui.fg
                            font.family: ui.font
                            font.pixelSize: ui.body
                            wrapMode: Text.Wrap
                            textFormat: Text.PlainText
                        }
                        OmaButton {
                            glyph: "\u{F0156}"
                            bordered: false
                            onClicked: backend.dismissMessage()
                            tooltip: qsTr("Dismiss")
                        }
                    }
                }
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
        }
    }
}
