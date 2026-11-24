import QtQuick
import QtQuick.Layouts
import RealityView

// The visualization viewport: the C++ scene item, an optional perspective tilt, and overlays.
Item {
    id: view
    required property VehicleViewModel vehicle
    required property FrameMonitor monitor
    property bool tilted: true
    clip: true

    function zoomBy(factor) {
        scene.pixelsPerMeter = Math.max(2, Math.min(40, scene.pixelsPerMeter * factor))
    }

    Rectangle { anchors.fill: parent; color: "#0e1014" }

    // Oversized so the tilted plane still covers the viewport.
    VehicleSceneItem {
        id: scene
        vehicle: view.vehicle
        width: view.width * 1.9
        height: view.height * 1.9
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: -view.height * 0.12
        egoAnchorY: 0.76
        pixelsPerMeter: 11

        transform: Rotation {
            origin.x: scene.width / 2
            origin.y: scene.height * 0.76
            axis { x: 1; y: 0; z: 0 }
            angle: view.tilted ? 52 : 0
            Behavior on angle { NumberAnimation { duration: 450; easing.type: Easing.OutCubic } }
        }
    }

    // Stale-data banner: the UI keeps rendering while the producer is stalled; it just
    // tells the user the picture is frozen instead of pretending.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 16
        visible: view.vehicle.stale && view.vehicle.hasData
        radius: 16
        height: 32
        width: staleText.implicitWidth + 32
        color: Qt.rgba(1, 0.3, 0.31, 0.16)
        border.color: Theme.bad
        Text {
            id: staleText
            anchors.centerIn: parent
            text: "Telemetry stale · " + Math.round(view.vehicle.dataAgeMs) + " ms · UI still at "
                  + Math.round(view.monitor.fps) + " fps"
            color: Theme.bad
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        SequentialAnimation on opacity {
            running: view.vehicle.stale
            loops: Animation.Infinite
            NumberAnimation { to: 0.55; duration: 500 }
            NumberAnimation { to: 1.0; duration: 500 }
        }
    }

    // Live HUD in the corner.
    Column {
        anchors { left: parent.left; bottom: parent.bottom; margins: 16 }
        spacing: 2
        Row {
            spacing: 6
            Rectangle {
                width: 8; height: 8; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: view.vehicle.stale ? Theme.bad : Theme.good
                // Independent animation: proves the GUI thread is alive even when data is not.
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.25; duration: 600 }
                    NumberAnimation { to: 1; duration: 600 }
                }
            }
            Text {
                text: Math.round(view.monitor.fps) + " fps · p99 " + view.monitor.p99Ms.toFixed(1) + " ms · "
                      + view.vehicle.measuredRateHz.toFixed(0) + " Hz telemetry"
                color: Theme.muted
                font.pixelSize: 11
                font.family: Theme.mono
            }
        }
        Text {
            text: "W/S throttle·brake  A/D steer  Space autopilot  Q/E lane change  T tilt  P panel  +/− zoom  R reset"
            color: Theme.faint
            font.pixelSize: 10
        }
    }

    Row {
        anchors { right: parent.right; bottom: parent.bottom; margins: 16 }
        spacing: 8
        PillButton { text: "−"; onClicked: view.zoomBy(1 / 1.2) }
        PillButton { text: "+"; onClicked: view.zoomBy(1.2) }
        PillButton { text: view.tilted ? "3D" : "2D"; active: view.tilted; onClicked: view.tilted = !view.tilted }
    }
}
