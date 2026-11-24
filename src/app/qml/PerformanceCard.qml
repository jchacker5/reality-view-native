import QtQuick
import QtQuick.Layouts
import RealityView

Card {
    id: card
    required property FrameMonitor monitor
    title: "Performance"
    subtitle: monitor.graphicsApi

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: Math.round(card.monitor.fps)
            readonly property real hz: 1000 / card.monitor.targetFrameMs
            color: card.monitor.fps >= hz * 0.95 ? Theme.good : (card.monitor.fps >= hz * 0.75 ? Theme.warn : Theme.bad)
            font.pixelSize: 34
            font.family: Theme.mono
        }
        Text { text: "fps"; color: Theme.muted; font.pixelSize: 12; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 6 }
        Item { Layout.fillWidth: true }
        ColumnLayout {
            spacing: 0
            Text { Layout.alignment: Qt.AlignRight; text: "jank (>1.5× " + card.monitor.targetFrameMs.toFixed(1) + " ms vsync)"; color: Theme.muted; font.pixelSize: 10 }
            Text {
                Layout.alignment: Qt.AlignRight
                text: card.monitor.windowJank + " / 240 frames · " + card.monitor.totalJank + " total"
                color: card.monitor.windowJank > 0 ? Theme.warn : Theme.text
                font.family: Theme.mono
                font.pixelSize: 12
            }
        }
    }

    SparklineItem {
        Layout.fillWidth: true
        Layout.preferredHeight: 56
        values: card.monitor.frameTimeSeries
        minValue: 0
        maxValue: card.monitor.targetFrameMs * 2.5
        threshold: card.monitor.targetFrameMs
        color: Theme.accent
    }

    StatRow { label: "Frame time p50 / p95 / p99"; value: card.monitor.p50Ms.toFixed(2) + " / " + card.monitor.p95Ms.toFixed(2) + " / " + card.monitor.p99Ms.toFixed(2) + " ms" }
    StatRow { label: "Worst frame (window)"; value: card.monitor.maxMs.toFixed(2) + " ms"; valueColor: card.monitor.maxMs > 25 ? Theme.warn : Theme.text }
    StatRow { label: "Scene-graph sync (GUI blocked)"; value: card.monitor.syncMs.toFixed(3) + " ms" }
    StatRow { label: "Process CPU (all threads)"; value: card.monitor.cpuPercent.toFixed(1) + " %" }
    StatRow { label: "Resident memory (peak)"; value: card.monitor.residentMb.toFixed(1) + " MB (" + card.monitor.peakResidentMb.toFixed(1) + ")" }
}
