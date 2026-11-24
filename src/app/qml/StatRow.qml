import QtQuick
import QtQuick.Layouts

RowLayout {
    property string label
    property string value
    property color valueColor: Theme.text
    Layout.fillWidth: true
    Text { text: parent.label; color: Theme.muted; font.pixelSize: 12 }
    Item { Layout.fillWidth: true }
    Text {
        text: parent.value
        color: parent.valueColor
        font.pixelSize: 12
        font.family: Theme.mono
    }
}
