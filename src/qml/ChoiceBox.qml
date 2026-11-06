import QtQuick
import QtQuick.Controls

// The shell's dropdown (qs.Ui Dropdown): an outlined box with the current
// choice and a chevron, opening a list with the popup border.
ComboBox {
    id: box
    property int maxWidth: Math.round(260 * ui.scale)
    property string placeholder

    width: Math.min(Math.max(metrics.advanceWidth + leftPadding + rightPadding, ui.s(90)), maxWidth)
    implicitHeight: ui.controlHeight
    // The same room on both sides, so the chevron never covers the text
    // whichever side it sits on in the reading direction.
    leftPadding: ui.s(18) + arrow.implicitWidth
    rightPadding: ui.s(18) + arrow.implicitWidth
    font.family: ui.font
    font.pixelSize: ui.body
    displayText: currentIndex < 0 && placeholder.length > 0 ? placeholder : currentText

    TextMetrics {
        id: metrics
        font: box.font
        text: box.displayText
    }

    contentItem: Text {
        text: box.displayText
        color: box.currentIndex < 0 ? ui.muted : ui.fg
        font: box.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
        textFormat: Text.PlainText
    }

    indicator: Text {
        id: arrow
        x: Qt.application.layoutDirection === Qt.RightToLeft ? ui.s(10) : box.width - width - ui.s(10)
        anchors.verticalCenter: parent.verticalCenter
        text: box.popup.visible ? "\u{F0143}" : "\u{F0140}"
        color: Qt.darker(ui.fg, 1.2)
        font.family: ui.iconFont
        font.pixelSize: ui.body
        textFormat: Text.PlainText
    }

    background: Rectangle {
        radius: theme.radius
        color: box.pressed ? ui.pressedFill : box.hovered || box.visualFocus ? ui.hoverFill : ui.normalFill
        border.width: 1
        border.color: box.visualFocus ? ui.accent : box.hovered ? ui.alpha(ui.fg, 0.55) : ui.normalBorder
    }

    delegate: ItemDelegate {
        id: option
        required property var modelData
        required property int index
        width: ListView.view ? ListView.view.width : box.width
        height: ui.controlHeight
        highlighted: box.highlightedIndex === index
        contentItem: Text {
            text: option.modelData[box.textRole] !== undefined ? option.modelData[box.textRole] : option.modelData
            color: option.index === box.currentIndex ? ui.accent : ui.fg
            font: box.font
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignLeft
            textFormat: Text.PlainText
        }
        background: Rectangle {
            color: option.highlighted ? ui.hoverFill : "transparent"
        }
    }

    popup: Popup {
        y: box.height + ui.s(2)
        width: Math.max(box.width, Math.min(box.maxWidth + ui.s(60), Math.round(380 * ui.scale)))
        implicitHeight: Math.min(list.contentHeight, ui.controlHeight * 10) + 2
        padding: 1
        contentItem: ListView {
            id: list
            clip: true
            implicitHeight: contentHeight
            model: box.popup.visible ? box.delegateModel : null
            currentIndex: box.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            color: ui.bg
            border.color: ui.accent
            border.width: 1
            radius: theme.radius
        }
    }
}
