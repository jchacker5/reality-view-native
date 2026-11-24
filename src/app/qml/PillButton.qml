import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    property color tint: Theme.accent
    property bool active: false

    implicitHeight: 34
    focusPolicy: Qt.NoFocus
    leftPadding: 14
    rightPadding: 14
    font.pixelSize: 12
    font.weight: Font.DemiBold

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? (control.active ? "#ffffff" : Theme.text) : Theme.faint
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: height / 2
        color: control.active ? control.tint
             : control.down ? Qt.darker(Theme.cardBorder, 1.3)
             : control.hovered ? "#262c37" : "#1f242d"
        border.color: control.active ? Qt.lighter(control.tint, 1.2) : Theme.cardBorder
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
