import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A modal in the shell's style (qs.Ui ConfirmDialog): the theme background
// with the accent border, a bold title, the content, and its buttons.
Popup {
    id: dialog
    property string title
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool showReject: true
    default property alias content: body.data
    signal accepted()
    signal rejected()

    function accept() { close(); accepted() }
    function reject() { close(); rejected() }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    padding: ui.s(18)
    width: Math.min(parent ? parent.width - ui.s(48) : 400, Math.round(400 * ui.scale))
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: ui.alpha(ui.bg, 0.6) }
    background: Rectangle {
        color: ui.bg
        border.color: ui.accent
        border.width: 1
        radius: theme.radius
    }

    contentItem: ColumnLayout {
        spacing: ui.s(14)
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        Text {
            Layout.fillWidth: true
            text: dialog.title
            horizontalAlignment: Text.AlignLeft
            color: ui.fg
            font.family: ui.font
            font.pixelSize: ui.title
            font.bold: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: ui.s(10)
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: ui.s(8)
            OmaButton {
                visible: dialog.showReject
                text: dialog.rejectText
                onClicked: dialog.reject()
            }
            OmaButton {
                text: dialog.acceptText
                highlighted: true
                onClicked: dialog.accept()
            }
        }
    }
}
