import QtQuick
import QtQuick.Layouts
import RealityView

Card {
    id: card
    required property VehicleViewModel vehicle
    required property DriverInput driver
    title: "Drive"
    subtitle: vehicle.replay ? "replay: controls disabled" : "simulated vehicle"
    enabled: !vehicle.replay

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        PillButton {
            Layout.fillWidth: true
            text: card.vehicle.autopilotState === 2 ? "Autopilot engaged" : "Engage Autopilot"
            active: card.vehicle.autopilotState === 2
            enabled: card.vehicle.autopilotState !== 0
            onClicked: card.vehicle.toggleAutopilot()
        }
        PillButton { text: "−5"; onClicked: card.vehicle.adjustCruise(-5) }
        Text {
            text: Math.round(card.vehicle.setSpeedMph) + " mph"
            color: Theme.text
            font.family: Theme.mono
            font.pixelSize: 12
        }
        PillButton { text: "+5"; onClicked: card.vehicle.adjustCruise(5) }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        PillButton {
            Layout.fillWidth: true
            text: "◀ Lane change"
            active: card.vehicle.turnSignal === 1
            onClicked: card.vehicle.signalTurn(card.vehicle.turnSignal === 1 ? 0 : 1)
        }
        PillButton {
            Layout.fillWidth: true
            text: "Lane change ▶"
            active: card.vehicle.turnSignal === 2
            onClicked: card.vehicle.signalTurn(card.vehicle.turnSignal === 2 ? 0 : 2)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            model: ["P", "R", "N", "D"]
            delegate: PillButton {
                required property string modelData
                Layout.fillWidth: true
                text: modelData
                active: card.vehicle.gear === modelData
                onClicked: card.vehicle.shiftGear(modelData)
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        HoldButton { Layout.fillWidth: true; text: "Steer ◀"; onHeld: (down) => card.driver.steeringLeft = down }
        HoldButton { Layout.fillWidth: true; text: "Accel"; tint: Theme.good; onHeld: (down) => card.driver.accelerating = down }
        HoldButton { Layout.fillWidth: true; text: "Brake"; tint: Theme.bad; onHeld: (down) => card.driver.braking = down }
        HoldButton { Layout.fillWidth: true; text: "Steer ▶"; onHeld: (down) => card.driver.steeringRight = down }
    }

    // Actuator readback: what the vehicle is actually doing, whoever is driving.
    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 4
        spacing: 16

        Item {
            implicitWidth: 64
            implicitHeight: 64
            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "transparent"
                border.width: 5
                border.color: card.vehicle.autopilotState === 2 ? Theme.accent : Theme.muted
                rotation: -card.vehicle.steeringWheelDeg
                Rectangle { anchors.centerIn: parent; width: parent.width - 10; height: 4; color: parent.border.color }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height / 2
                    width: 4
                    height: parent.height / 2 - 5
                    color: parent.border.color
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: [
                    { name: "Throttle", value: card.vehicle.throttle, tint: Theme.good },
                    { name: "Brake", value: card.vehicle.brake, tint: Theme.bad }
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text { text: modelData.name; color: Theme.muted; font.pixelSize: 11; Layout.preferredWidth: 56 }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 6
                        radius: 3
                        color: "#232934"
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, modelData.value))
                            height: parent.height
                            radius: 3
                            color: modelData.tint
                        }
                    }
                }
            }
            StatRow { label: "Steering wheel"; value: card.vehicle.steeringWheelDeg.toFixed(1) + "°" }
        }
    }
}
