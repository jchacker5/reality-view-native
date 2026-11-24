import QtQuick
import QtQuick.Layouts
import RealityView

Card {
    id: card
    required property VehicleViewModel vehicle
    title: "Vehicle telemetry"
    subtitle: "seed " + vehicle.roadSeed

    SparklineItem {
        Layout.fillWidth: true
        Layout.preferredHeight: 56
        values: card.vehicle.speedSeries
        minValue: 0
        maxValue: 80
        threshold: card.vehicle.autopilotState === 2 ? card.vehicle.setSpeedMph : NaN
        color: Theme.good
        thresholdColor: Theme.accent
    }
    Text { text: "Speed, last 30 s (mph) · blue line = set speed"; color: Theme.faint; font.pixelSize: 10 }

    StatRow { label: "Position"; value: card.vehicle.latitude.toFixed(6) + ", " + card.vehicle.longitude.toFixed(6) }
    StatRow { label: "Heading"; value: card.vehicle.headingDeg.toFixed(1) + "°" }
    StatRow { label: "Acceleration"; value: card.vehicle.accelMps2.toFixed(2) + " m/s²" }
    StatRow { label: "Lane / offset"; value: (card.vehicle.lane + 1) + " / " + card.vehicle.laneOffsetM.toFixed(2) + " m" }
    StatRow { label: "Tracked objects"; value: card.vehicle.objectCount }
    StatRow { label: "Trip"; value: card.vehicle.odometerKm.toFixed(2) + " km · " + card.vehicle.simTime.toFixed(0) + " s" }
}
