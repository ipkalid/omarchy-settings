import QtQuick

SettingsPage {
    id: page
    pane: "pointer"
    title: win.caps.touchpad ? qsTr("Mouse & Trackpad") : qsTr("Mouse")
    tagline: win.caps.touchpad ? qsTr("Mouse and trackpad") : qsTr("Mouse")
    glyph: "\u{F037D}"
    description: qsTr("Set how fast the pointer moves, which way scrolling goes, and what taps and swipes do.")
    lists: ["power"]

    function times(v) { return qsTr("%1×").arg(v.toLocaleString(Qt.locale(), 'f', 1)) }

    SettingsGroup {
        title: qsTr("Mouse")
        SliderRow {
            setting: "pointer.sensitivity"
            label: qsTr("Tracking speed")
            from: -1; to: 1; stepSize: 0.05
            fromText: qsTr("Slow"); toText: qsTr("Fast")
        }
        ChoiceRow {
            setting: "pointer.acceleration"
            label: qsTr("Pointer acceleration")
            sublabel: qsTr("Flat moves the pointer the same distance however fast you move")
            options: [{ value: "adaptive", text: qsTr("Adaptive") }, { value: "flat", text: qsTr("Flat") }]
        }
        ToggleRow {
            setting: "pointer.naturalScroll"
            label: qsTr("Natural scrolling")
            sublabel: qsTr("Content follows your fingers, as on a phone")
        }
        SliderRow {
            setting: "pointer.scrollSpeed"
            label: qsTr("Scrolling speed")
            from: 0.2; to: 3; stepSize: 0.1
            format: page.times
        }
        ToggleRow { setting: "pointer.leftHanded"; label: qsTr("Swap left and right buttons") }
    }

    SettingsGroup {
        title: qsTr("Trackpad")
        footer: win.caps.touchpad ? qsTr("Swiping uses three fingers left and right.")
                                  : qsTr("No trackpad is connected. These apply when one is.")
        ToggleRow {
            visible: win.caps.touchpad === true
            setting: "pointer.touchpad"
            label: qsTr("Use the trackpad")
        }
        ToggleRow { setting: "trackpad.tapToClick"; label: qsTr("Tap to click") }
        ToggleRow { setting: "trackpad.twoFingerClick"; label: qsTr("Secondary click with two fingers") }
        ToggleRow { setting: "trackpad.naturalScroll"; label: qsTr("Natural scrolling") }
        SliderRow {
            setting: "trackpad.scrollSpeed"
            label: qsTr("Scrolling speed")
            from: 0.1; to: 3; stepSize: 0.05
            format: page.times
        }
        ToggleRow { setting: "trackpad.disableWhileTyping"; label: qsTr("Ignore the trackpad while typing") }
        ToggleRow { setting: "trackpad.threeFingerDrag"; label: qsTr("Three-finger drag") }
        ToggleRow { setting: "trackpad.swipeWorkspaces"; label: qsTr("Swipe between workspaces") }
    }

    SettingsGroup {
        visible: (backend.lists.power || {}).touchscreen === true
        title: qsTr("Touchscreen")
        ToggleRow { setting: "pointer.touchscreen"; label: qsTr("Use the touchscreen") }
    }
}
