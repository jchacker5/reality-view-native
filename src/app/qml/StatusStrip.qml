import QtQuick
import QtQuick.Layouts
import RealityView

// Top of the vehicle panel: gear, speed, driver-assist state, turn signals.
Rectangle {
    id: strip
    required property VehicleViewModel vehicle
    readonly property bool blinkOn: (vehicle.simTime % 0.7) < 0.35

    implicitHeight: 96
    color: Theme.panel

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        spacing: 24

        Row {
            spacing: 10
            Repeater {
                model: ["P", "R", "N", "D"]
                delegate: Text {
                    required property string modelData
                    text: modelData
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: strip.vehicle.gear === modelData ? Theme.text : Theme.faint
                }
            }
        }

        Text {
            text: "◀"
            font.pixelSize: 22
            color: strip.vehicle.turnSignal === 1 && strip.blinkOn ? Theme.good : Theme.faint
        }

        ColumnLayout {
            spacing: -4
            Layout.alignment: Qt.AlignVCenter
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: Math.round(strip.vehicle.speedMph)
                color: Theme.text
                font.pixelSize: 54
                font.weight: Font.Light
                font.family: Theme.mono
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "MPH"
                color: Theme.muted
                font.pixelSize: 11
                font.letterSpacing: 2
            }
        }

        Text {
            text: "▶"
            font.pixelSize: 22
            color: strip.vehicle.turnSignal === 2 && strip.blinkOn ? Theme.good : Theme.faint
        }

        Item { Layout.fillWidth: true }

        // Driver-assist badge: grey = unavailable, outline = standby, blue = active.
        RowLayout {
            spacing: 10
            Rectangle {
                width: 34
                height: 34
                radius: 17
                color: strip.vehicle.autopilotState === 2 ? Theme.accent : "transparent"
                border.width: 2
                border.color: strip.vehicle.autopilotState === 0 ? Theme.faint
                            : strip.vehicle.autopilotState === 1 ? Theme.muted : Theme.accent
                Rectangle { anchors.centerIn: parent; width: 16; height: 2; color: Theme.text }
                Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y: 17; width: 2; height: 10; color: Theme.text }
            }
            ColumnLayout {
                spacing: 0
                Text {
                    text: strip.vehicle.replay ? "Replay" : "Autopilot"
                    color: Theme.text
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
                Text {
                    text: strip.vehicle.autopilotState === 2
                          ? "Set " + Math.round(strip.vehicle.setSpeedMph) + " mph"
                          : strip.vehicle.autopilotText
                    color: strip.vehicle.autopilotState === 2 ? Theme.accent : Theme.muted
                    font.pixelSize: 12
                }
            }
        }
    }
}
