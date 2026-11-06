import QtQuick
import QtQuick.Layouts

// What sits on the bar and where. Moves go through `omarchy bar move` and
// adding and removing through `omarchy plugin enable/disable`, so the shell
// stays the one that writes its layout.
SettingsPage {
    id: page
    pane: "bar"
    title: qsTr("Bar")
    tagline: qsTr("%n widgets", "", page.placedCount)
    glyph: "\u{F06EB}"
    description: qsTr("Choose what the bar shows and arrange it. Changes appear on the bar right away.")
    lists: ["bar", "hardware"]

    readonly property var bar: backend.lists.bar || ({})
    readonly property var sections: bar.sections || ({})
    readonly property var available: bar.available || []
    readonly property bool working: ["bar.move", "bar.add", "bar.remove"].some(a => backend.busy.indexOf(a) >= 0)
    readonly property var order: ["left", "centerBegin", "center", "centerEnd", "right", "tray"]
    readonly property var sectionNames: ({
        left: qsTr("Left"), centerBegin: qsTr("Center, before the clock"), center: qsTr("Center"),
        centerEnd: qsTr("Center, after the clock"), right: qsTr("Right"), tray: qsTr("Tray drawer")
    })
    readonly property int placedCount: order.reduce((n, s) => n + (sections[s] || []).length, 0)
    function move(widget, from, to, index) { backend.act("bar.move", [widget.id, from, widget.index, to, index], ["bar"]) }

    SettingsGroup {
        separated: true
        ToggleRow { setting: "desktop.barVisible"; label: qsTr("Show the bar") }
        ChoiceRow {
            setting: "desktop.barPosition"
            label: qsTr("Position on screen")
            options: [
                { value: "top", text: qsTr("Top") }, { value: "bottom", text: qsTr("Bottom") },
                { value: "left", text: qsTr("Left") }, { value: "right", text: qsTr("Right") }
            ]
        }
        ToggleRow {
            visible: (backend.lists.hardware || {}).laptop === true
            setting: "bar.batteryPercentage"
            label: qsTr("Show battery percentage")
        }
        ChoiceRow {
            setting: "appearance.barBackground"
            label: qsTr("Bar background")
            options: [
                { value: "opaque", text: qsTr("Opaque") },
                { value: "glassy", text: qsTr("Glassy") },
                { value: "none", text: qsTr("None") }
            ]
        }
    }

    // A small picture of the bar as it is laid out now. The bar itself is
    // never mirrored, so neither is its picture.
    Rectangle {
        Layout.fillWidth: true
        LayoutMirroring.enabled: false
        LayoutMirroring.childrenInherit: true
        implicitHeight: preview.implicitHeight + ui.s(12)
        radius: theme.radius
        color: ui.normalFill
        border.width: 1
        border.color: ui.normalBorder

        RowLayout {
            id: preview
            anchors.fill: parent
            anchors.margins: ui.s(6)
            spacing: ui.s(6)
            component Chips: Flow {
                property var names: []
                spacing: ui.s(4)
                Repeater {
                    model: parent.names
                    delegate: Rectangle {
                        required property string modelData
                        width: chip.implicitWidth + ui.s(8)
                        height: chip.implicitHeight + ui.s(4)
                        radius: theme.radius
                        color: ui.hoverFill
                        Text {
                            id: chip
                            anchors.centerIn: parent
                            text: parent.modelData
                            color: ui.fg
                            font.family: ui.font
                            font.pixelSize: ui.caption
                            textFormat: Text.PlainText
                        }
                    }
                }
            }
            Chips { Layout.fillWidth: true; Layout.preferredWidth: 1; names: (page.sections.left || []).map(w => w.name) }
            Chips {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                names: ["centerBegin", "center", "centerEnd"].reduce((all, s) => all.concat((page.sections[s] || []).map(w => w.name)), [])
            }
            Chips {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                names: (page.sections.right || []).map(w => w.name)
            }
        }
    }

    Repeater {
        model: page.order
        delegate: SettingsGroup {
            id: group
            required property string modelData
            readonly property var widgets: page.sections[modelData] || []
            Layout.fillWidth: true
            title: page.sectionNames[modelData]
            footer: modelData === "tray" ? qsTr("Widgets here open from the tray's drawer instead of sitting on the bar.")
                  : modelData === "center" ? qsTr("Put widgets beside the clock in the sections before and after it, or the clock moves off center.")
                  : widgets.length === 0 ? qsTr("Nothing here yet.") : ""

            Repeater {
                model: group.widgets
                delegate: SettingRow {
                    id: wrow
                    required property var modelData
                    label: modelData.name
                    sublabel: modelData.description || modelData.id
                    OmaButton {
                        glyph: "\u{F0143}"
                        bordered: false
                        enabled: !page.working && wrow.modelData.index > 0
                        tooltip: qsTr("Move up")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: page.move(wrow.modelData, group.modelData, group.modelData, wrow.modelData.index - 1)
                    }
                    OmaButton {
                        glyph: "\u{F0140}"
                        bordered: false
                        enabled: !page.working && wrow.modelData.index < group.widgets.length - 1
                        tooltip: qsTr("Move down")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: page.move(wrow.modelData, group.modelData, group.modelData, wrow.modelData.index + 1)
                    }
                    ChoiceBox {
                        enabled: !page.working
                        maxWidth: Math.round(220 * ui.scale)
                        model: page.order.map(s => ({ value: s, text: page.sectionNames[s] }))
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: page.order.indexOf(group.modelData)
                        onActivated: {
                            if (currentValue !== group.modelData)
                                page.move(wrow.modelData, group.modelData, currentValue, (page.sections[currentValue] || []).length)
                            currentIndex = Qt.binding(() => page.order.indexOf(group.modelData))
                        }
                        Accessible.name: qsTr("Section")
                    }
                    OmaButton {
                        glyph: "\u{F0156}"
                        bordered: false
                        enabled: !page.working
                        tooltip: qsTr("Remove from the bar")
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: backend.act("bar.remove", [wrow.modelData.id], ["bar", "plugins"])
                    }
                }
            }
        }
    }

    SettingsGroup {
        title: qsTr("Add to the Bar")
        footer: page.available.length === 0 ? qsTr("Every widget is on the bar already. Plugins adds more.")
                                            : qsTr("Removing a widget turns its plugin off; adding it turns it back on.")
        SettingRow {
            visible: page.available.length > 0
            label: qsTr("Widget")
            ChoiceBox {
                id: widget
                maxWidth: Math.round(220 * ui.scale)
                model: page.available
                textRole: "name"
                valueRole: "id"
                currentIndex: -1
                placeholder: qsTr("Choose a widget")
                onCountChanged: Qt.callLater(() => currentIndex = -1)
                Accessible.name: qsTr("Widget to add")
            }
            ChoiceBox {
                id: where
                maxWidth: Math.round(220 * ui.scale)
                model: page.order.map(s => ({ value: s, text: page.sectionNames[s] }))
                textRole: "text"
                valueRole: "value"
                currentIndex: page.order.indexOf("right")
                Accessible.name: qsTr("Section")
            }
            OmaButton {
                text: qsTr("Add")
                enabled: widget.currentIndex >= 0 && !page.working
                anchors.verticalCenter: parent.verticalCenter
                onClicked: backend.act("bar.add", [widget.currentValue, where.currentValue], ["bar", "plugins"])
            }
        }
    }
}
