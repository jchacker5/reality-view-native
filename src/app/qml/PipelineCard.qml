import QtQuick
import QtQuick.Layouts
import RealityView

Card {
    id: card
    required property VehicleViewModel vehicle
    title: "Telemetry pipeline"
    subtitle: vehicle.producerStalled ? "producer stalled" : (vehicle.stale ? "stale" : "healthy")

    Text { text: "Publish rate"; color: Theme.muted; font.pixelSize: 11 }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: [60, 240, 1000, 4000]
            delegate: PillButton {
                required property int modelData
                Layout.fillWidth: true
                text: modelData >= 1000 ? (modelData / 1000) + " kHz" : modelData + " Hz"
                active: card.vehicle.publishRateHz === modelData
                onClicked: card.vehicle.setPublishRate(modelData)
            }
        }
    }

    StatRow { label: "Measured rate"; value: card.vehicle.measuredRateHz.toFixed(0) + " Hz" }
    StatRow { label: "Samples coalesced per frame"; value: card.vehicle.samplesPerFrame.toFixed(1) + " (max " + card.vehicle.maxBatch + ")" }
    StatRow { label: "Drain cost per frame"; value: card.vehicle.drainMicros.toFixed(1) + " µs" }
    StatRow { label: "Queue depth"; value: card.vehicle.queueDepth + " / 4096" }
    StatRow {
        label: "Data age"
        value: card.vehicle.dataAgeMs.toFixed(0) + " ms"
        valueColor: card.vehicle.stale ? Theme.bad : Theme.text
    }
    StatRow {
        label: "Sequence gaps (dropped)"
        value: card.vehicle.totalGaps.toFixed(0)
        valueColor: card.vehicle.totalGaps > 0 ? Theme.warn : Theme.text
    }
    StatRow {
        label: "Queue overflows"
        value: card.vehicle.overflowCount.toFixed(0)
        valueColor: card.vehicle.overflowCount > 0 ? Theme.bad : Theme.text
    }

    Text { text: "Fault injection"; color: Theme.muted; font.pixelSize: 11; Layout.topMargin: 4 }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        PillButton {
            Layout.fillWidth: true
            text: "Stall 500 ms"
            tint: Theme.warn
            active: card.vehicle.producerStalled && card.vehicle.lastStallMs === 500
            onClicked: card.vehicle.injectStall(500)
        }
        PillButton {
            Layout.fillWidth: true
            text: "Stall 3 s"
            tint: Theme.warn
            active: card.vehicle.producerStalled && card.vehicle.lastStallMs === 3000
            onClicked: card.vehicle.injectStall(3000)
        }
        PillButton {
            Layout.fillWidth: true
            text: "Drop 30%"
            tint: Theme.warn
            active: card.vehicle.dropoutEnabled
            onClicked: card.vehicle.setDropout(!card.vehicle.dropoutEnabled)
        }
    }
}
