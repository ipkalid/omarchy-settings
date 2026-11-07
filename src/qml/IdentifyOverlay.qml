import QtQuick
import QtQuick.Window

// Shows each display's number, big, on the display itself for a few
// seconds, so the boxes in the arrangement can be matched to the screens on
// the desk. A click or Esc on any of them puts them all away.
Item {
    id: overlay
    // [{name, number, label}] for the displays that are on.
    property var monitors: []
    property bool showing: false

    function show() {
        showing = false
        showing = true
        hide.restart()
    }
    function close() {
        hide.stop()
        showing = false
    }

    Timer { id: hide; interval: 3000; onTriggered: overlay.showing = false }

    Instantiator {
        model: overlay.showing ? Qt.application.screens : []
        delegate: Window {
            id: shown
            required property var modelData
            readonly property var monitor: overlay.monitors.find(m => m.name === modelData.name)
            // Hyprland opens new windows on the focused display whatever
            // screen they ask for, so each one opens hidden and the backend
            // moves it to its display and makes it full screen; it shows
            // once it is there.
            property bool placed: false
            screen: modelData
            title: "omasettings-identify-" + modelData.name
            flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
            width: 320; height: 240
            color: placed ? Qt.rgba(ui.bg.r, ui.bg.g, ui.bg.b, 0.82) : "transparent"
            visible: monitor !== undefined
            onFrameSwapped: {
                if (requested) return
                requested = true
                backend.act("monitors.identify", [modelData.name])
                reveal.start()
            }
            property bool requested: false
            Timer { id: reveal; interval: 250; onTriggered: shown.placed = true }

            Item {
                anchors.fill: parent
                visible: shown.placed
                focus: true
                Keys.onEscapePressed: overlay.close()
                TapHandler { onTapped: overlay.close() }

                Column {
                    anchors.centerIn: parent
                    spacing: ui.s(12)
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: shown.monitor ? Number(shown.monitor.number).toLocaleString(Qt.locale(), 'f', 0) : ""
                        color: ui.accent
                        font.family: ui.font
                        font.pixelSize: Math.round(Math.min(shown.width, shown.height) * 0.3)
                        font.bold: true
                        textFormat: Text.PlainText
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: shown.monitor ? shown.monitor.label : ""
                        color: ui.fg
                        font.family: ui.font
                        font.pixelSize: ui.heading * 2
                        textFormat: Text.PlainText
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: shown.modelData.name
                        color: ui.muted
                        font.family: ui.font
                        font.pixelSize: ui.heading
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }
}
