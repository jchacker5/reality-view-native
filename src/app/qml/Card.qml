import QtQuick
import QtQuick.Layouts

// A titled panel. Children go into a ColumnLayout.
Rectangle {
    id: card
    property string title
    property string subtitle
    default property alias content: body.data

    implicitHeight: body.implicitHeight + header.implicitHeight + 36
    radius: Theme.radius
    color: Theme.card
    border.color: Theme.cardBorder

    RowLayout {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
        Text {
            text: card.title
            color: Theme.text
            font.pixelSize: 13
            font.weight: Font.DemiBold
            font.letterSpacing: 0.6
            font.capitalization: Font.AllUppercase
        }
        Item { Layout.fillWidth: true }
        Text { text: card.subtitle; color: Theme.muted; font.pixelSize: 11 }
    }
    ColumnLayout {
        id: body
        anchors { left: parent.left; right: parent.right; top: header.bottom; margins: 14; topMargin: 10 }
        spacing: 8
    }
}
