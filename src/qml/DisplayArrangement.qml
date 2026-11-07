import QtQuick

// The displays drawn where they sit, to scale. Drag one to move it: while
// dragging, an outline shows where it will land (its edges snap to the
// others', and it always ends up touching one), and dropping it sends the
// whole new layout. The picture is never mirrored for right-to-left
// languages, since it shows the desk.
Item {
    id: canvas
    // [{key, x, y, w, h, label, detail, number, primary, mirrors: "names"}],
    // with x, y, w, h in Hyprland's layout coordinates.
    property var monitors: []
    property string selectedKey
    signal picked(string key)
    // [{key, x, y}] for every display, when a drop moved anything.
    signal arranged(var positions)

    LayoutMirroring.enabled: false
    LayoutMirroring.childrenInherit: true

    // Where each display shows after a drop, until the real layout comes
    // back from Hyprland.
    property var placed: null
    onMonitorsChanged: placed = null
    // Boxes glide only when a display is dropped (or a drag is called off),
    // never when the page opens, the window resizes, or the list reloads:
    // the first frame of a new box can come out at the wrong size, and
    // animating from it showed the display shrinking into place.
    property bool gliding: false
    function glide() { gliding = true; glideEnd.restart() }
    Timer { id: glideEnd; interval: 300; onTriggered: canvas.gliding = false }
    property int dragging: -1
    property var ghost: null

    function at(i) {
        return canvas.placed && canvas.placed[i] ? canvas.placed[i] : { x: canvas.monitors[i].x, y: canvas.monitors[i].y }
    }
    readonly property var bounds: {
        let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9
        for (let i = 0; i < monitors.length; ++i) {
            const p = at(i), m = monitors[i]
            x0 = Math.min(x0, p.x); y0 = Math.min(y0, p.y)
            x1 = Math.max(x1, p.x + m.w); y1 = Math.max(y1, p.y + m.h)
        }
        return x1 > x0 ? { x: x0, y: y0, w: x1 - x0, h: y1 - y0 } : { x: 0, y: 0, w: 1, h: 1 }
    }
    // Room around the layout to drag a display beside the others.
    readonly property real factor: Math.min(width / bounds.w, height / bounds.h) * (monitors.length > 1 ? 0.62 : 0.8)
    readonly property real ox: (width - bounds.w * factor) / 2 - bounds.x * factor
    readonly property real oy: (height - bounds.h * factor) / 2 - bounds.y * factor
    // How close, on screen, an edge has to come to snap.
    readonly property int snapPixels: ui.s(14)

    function rects() {
        return monitors.map((m, i) => ({ x: at(i).x, y: at(i).y, width: m.w, height: m.h }))
    }
    // Where display `i` lands if dropped `dx`, `dy` screen pixels away, in
    // the same coordinates as now (place() starts the layout at 0,0, so
    // shift back by how far another display moved).
    function landing(i, dx, dy) {
        const p = at(i)
        const result = backend.placeMonitor(rects(), i, Math.round(p.x + dx / factor), Math.round(p.y + dy / factor),
                                            Math.round(snapPixels / factor))
        return result
    }
    function drop(i, dx, dy) {
        const result = landing(i, dx, dy)
        const before = monitors.map((m, j) => at(j))
        placed = result
        if (result.some((p, j) => p.x !== before[j].x || p.y !== before[j].y))
            arranged(result.map((p, j) => ({ key: monitors[j].key, x: p.x, y: p.y })))
    }

    // Where the dragged display will land.
    Rectangle {
        visible: canvas.ghost !== null
        x: canvas.ghost ? canvas.ox + canvas.ghost.x * canvas.factor : 0
        y: canvas.ghost ? canvas.oy + canvas.ghost.y * canvas.factor : 0
        width: canvas.ghost ? canvas.ghost.w * canvas.factor : 0
        height: canvas.ghost ? canvas.ghost.h * canvas.factor : 0
        radius: Math.min(4, theme.radius)
        color: "transparent"
        border.color: ui.accent
        border.width: 2
        opacity: 0.8
    }

    Repeater {
        model: canvas.monitors
        delegate: Rectangle {
            id: box
            required property var modelData
            required property int index
            readonly property bool chosen: modelData.key === canvas.selectedKey
            readonly property var pos: canvas.at(index)
            property real dx: 0
            property real dy: 0

            x: canvas.ox + pos.x * canvas.factor + 2 + dx
            y: canvas.oy + pos.y * canvas.factor + 2 + dy
            z: mouse.dragging ? 2 : 1
            width: Math.max(8, modelData.w * canvas.factor - 4)
            height: Math.max(8, modelData.h * canvas.factor - 4)
            radius: Math.min(4, theme.radius)
            color: Qt.rgba(ui.accent.r, ui.accent.g, ui.accent.b, chosen ? 0.35 : 0.14)
            border.color: chosen || mouse.containsMouse ? ui.accent : ui.normalBorder
            border.width: chosen ? 2 : 1
            opacity: mouse.dragging ? 0.75 : 1

            Behavior on x { enabled: canvas.gliding && !mouse.dragging; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Behavior on y { enabled: canvas.gliding && !mouse.dragging; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

            Accessible.role: Accessible.Button
            Accessible.name: modelData.label
            Accessible.description: modelData.detail

            // The main display carries the menu bar, as on a Mac.
            Rectangle {
                visible: box.modelData.primary === true
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 3 }
                height: Math.max(3, Math.round(parent.height * 0.06))
                radius: Math.min(2, theme.radius)
                color: ui.fg
                opacity: 0.55
            }

            Rectangle {
                id: badge
                x: ui.s(6)
                y: ui.s(6) + (box.modelData.primary === true ? Math.max(3, Math.round(box.height * 0.06)) : 0)
                width: Math.max(ui.s(18), number.implicitWidth + ui.s(8))
                height: ui.s(18)
                radius: Math.min(height / 2, theme.radius > 0 ? height / 2 : 0)
                color: box.chosen ? ui.accent : ui.selectedFill
                visible: box.width > ui.s(40) && box.height > ui.s(30)
                Text {
                    id: number
                    anchors.centerIn: parent
                    text: Number(box.modelData.number).toLocaleString(Qt.locale(), 'f', 0)
                    color: box.chosen ? ui.bg : ui.fg
                    font.family: ui.font
                    font.pixelSize: ui.caption
                    font.bold: true
                    textFormat: Text.PlainText
                }
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - ui.s(12)
                spacing: ui.s(2)
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: box.modelData.label
                    color: ui.fg
                    font.family: ui.font
                    font.pixelSize: ui.small
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Text {
                    width: parent.width
                    visible: box.height > ui.s(56)
                    horizontalAlignment: Text.AlignHCenter
                    text: box.modelData.detail
                    color: ui.muted
                    font.family: ui.font
                    font.pixelSize: ui.caption
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Text {
                    width: parent.width
                    visible: box.modelData.mirrors.length > 0 && box.height > ui.s(72)
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("Mirrored on %1").arg(box.modelData.mirrors)
                    color: ui.muted
                    font.family: ui.font
                    font.pixelSize: ui.caption
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }

            // A MouseArea rather than a DragHandler: it can keep the page's
            // scrolling from taking the drag over.
            MouseArea {
                id: mouse
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true
                property bool dragging: false
                property point start
                readonly property bool movable: canvas.monitors.length > 1
                cursorShape: movable ? (dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor) : Qt.PointingHandCursor
                onPressed: mouseEvent => {
                    start = mapToItem(canvas, mouseEvent.x, mouseEvent.y)
                    canvas.picked(box.modelData.key)
                }
                onPositionChanged: mouseEvent => {
                    if (!pressed || !movable) return
                    const p = mapToItem(canvas, mouseEvent.x, mouseEvent.y)
                    const dx = p.x - start.x, dy = p.y - start.y
                    if (!dragging && Math.hypot(dx, dy) < Qt.styleHints.startDragDistance) return
                    if (!dragging) {
                        dragging = true
                        canvas.dragging = box.index
                    }
                    box.dx = dx
                    box.dy = dy
                    const result = canvas.landing(box.index, dx, dy)
                    // Back into the coordinates on screen now: place() starts
                    // the layout at 0,0, so undo that by another display's
                    // shift (any display but this one stays put).
                    const other = box.index === 0 ? 1 : 0
                    const shiftX = result[other].x - canvas.at(other).x, shiftY = result[other].y - canvas.at(other).y
                    canvas.ghost = { x: result[box.index].x - shiftX, y: result[box.index].y - shiftY,
                                     w: box.modelData.w, h: box.modelData.h }
                }
                onReleased: finish(true)
                onCanceled: finish(false)
                function finish(drop) {
                    if (!dragging) return
                    dragging = false
                    canvas.ghost = null
                    canvas.dragging = -1
                    canvas.glide()
                    // x and y animate from where the box was let go to where
                    // it lands (or back, when the drag was cancelled).
                    if (drop)
                        canvas.drop(box.index, box.dx, box.dy)
                    box.dx = 0
                    box.dy = 0
                }
            }
        }
    }
}
