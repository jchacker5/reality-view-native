import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import RealityView

ApplicationWindow {
    id: root
    required property VehicleViewModel vehicle
    required property FrameMonitor monitor
    property bool startTilted: true
    property bool showPanel: true

    width: 1480
    height: 900
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    color: Theme.bg
    title: qsTr("Reality View Native")

    // The frame clock. Runs once per vsync on the GUI thread, right before the scene graph
    // syncs, so telemetry is drained exactly once per rendered frame.
    FrameAnimation {
        running: true
        onTriggered: {
            root.vehicle.tick()
            driver.frame(frameTime)
        }
    }

    DriverInput {
        id: driver
        anchors.fill: parent
        focus: true
        vehicle: root.vehicle
        onZoomRequested: (factor) => scene.zoomBy(factor)
        onTiltToggled: scene.tilted = !scene.tilted
        onPanelToggled: root.showPanel = !root.showPanel
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Left: the vehicle visualization --------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.panel

            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                StatusStrip {
                    Layout.fillWidth: true
                    vehicle: root.vehicle
                }
                SceneView {
                    id: scene
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    vehicle: root.vehicle
                    monitor: root.monitor
                    tilted: root.startTilted
                }
            }
        }

        // ---- Right: controls and instrumentation ----------------------------------------
        ScrollView {
            visible: root.showPanel
            Layout.preferredWidth: 440
            Layout.fillHeight: true
            contentWidth: availableWidth
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                width: parent.width
                spacing: Theme.gap

                Item { Layout.preferredHeight: 4 }
                DriveCard {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gap
                    Layout.rightMargin: Theme.gap
                    vehicle: root.vehicle
                    driver: driver
                }
                PerformanceCard {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gap
                    Layout.rightMargin: Theme.gap
                    monitor: root.monitor
                }
                PipelineCard {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gap
                    Layout.rightMargin: Theme.gap
                    vehicle: root.vehicle
                }
                TelemetryCard {
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.gap
                    Layout.rightMargin: Theme.gap
                    vehicle: root.vehicle
                }
                Item { Layout.preferredHeight: 4 }
            }
        }
    }
}
