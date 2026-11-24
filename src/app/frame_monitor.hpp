#pragma once

#include "frame_stats.hpp"
#include "process_stats.hpp"
#include "spsc_ring_buffer.hpp"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <chrono>

class QQuickWindow;

namespace rvn {

/// Measures real frame pacing and process cost.
///
/// QQuickWindow::frameSwapped and the sync/render signals are emitted on the *render* thread
/// with the threaded render loop. We connect them with Qt::DirectConnection, time them there,
/// and hand the results to the GUI thread through an SPSC queue: the render thread never takes
/// a lock and never touches QObject state owned by the GUI thread. A 4 Hz GUI-thread timer
/// aggregates them into percentiles.
class FrameMonitor : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created in C++ and injected as a required property")

    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(double p50Ms READ p50Ms NOTIFY statsChanged)
    Q_PROPERTY(double p95Ms READ p95Ms NOTIFY statsChanged)
    Q_PROPERTY(double p99Ms READ p99Ms NOTIFY statsChanged)
    Q_PROPERTY(double maxMs READ maxMs NOTIFY statsChanged)
    Q_PROPERTY(double syncMs READ syncMs NOTIFY statsChanged)
    Q_PROPERTY(int windowJank READ windowJank NOTIFY statsChanged)
    Q_PROPERTY(double totalJank READ totalJank NOTIFY statsChanged)
    Q_PROPERTY(double totalFrames READ totalFrames NOTIFY statsChanged)
    Q_PROPERTY(double cpuPercent READ cpuPercent NOTIFY statsChanged)
    Q_PROPERTY(double residentMb READ residentMb NOTIFY statsChanged)
    Q_PROPERTY(double peakResidentMb READ peakResidentMb NOTIFY statsChanged)
    Q_PROPERTY(QList<float> frameTimeSeries READ frameTimeSeries NOTIFY statsChanged)
    Q_PROPERTY(QString graphicsApi READ graphicsApi NOTIFY graphicsApiChanged)
    Q_PROPERTY(double targetFrameMs READ targetFrameMs NOTIFY targetChanged)

public:
    explicit FrameMonitor(QObject* parent = nullptr);

    void attach(QQuickWindow* window);

    /// Machine-readable snapshot used by --benchmark-seconds.
    Q_INVOKABLE QString summaryJson() const;
    Q_INVOKABLE void resetStats();

    [[nodiscard]] double fps() const { return summary_.fps; }
    [[nodiscard]] double p50Ms() const { return summary_.p50Ms; }
    [[nodiscard]] double p95Ms() const { return summary_.p95Ms; }
    [[nodiscard]] double p99Ms() const { return summary_.p99Ms; }
    [[nodiscard]] double maxMs() const { return summary_.maxMs; }
    [[nodiscard]] double syncMs() const { return syncMs_; }
    [[nodiscard]] int windowJank() const { return static_cast<int>(summary_.windowJank); }
    [[nodiscard]] double totalJank() const { return static_cast<double>(summary_.totalJank); }
    [[nodiscard]] double totalFrames() const { return static_cast<double>(summary_.totalFrames); }
    [[nodiscard]] double cpuPercent() const { return cpuPercent_; }
    [[nodiscard]] double residentMb() const { return residentMb_; }
    [[nodiscard]] double peakResidentMb() const { return peakResidentMb_; }
    [[nodiscard]] QList<float> frameTimeSeries() const { return series_; }
    [[nodiscard]] QString graphicsApi() const { return graphicsApi_; }
    [[nodiscard]] double targetFrameMs() const { return stats_.targetFrameMs(); }

signals:
    void statsChanged();
    void graphicsApiChanged();
    void targetChanged();

private:
    struct FrameTiming {
        float intervalMs;
        float syncMs;
    };
    using Clock = std::chrono::steady_clock;

    void aggregate();

    // Render-thread state (only touched from render-thread slots).
    Clock::time_point lastSwap_{};
    Clock::time_point syncStart_{};
    float lastSyncMs_{0.0F};
    // Render thread -> GUI thread.
    SpscRingBuffer<FrameTiming, 1024> timings_;

    // GUI-thread state.
    QPointer<QQuickWindow> window_;
    QTimer timer_;
    FrameStats stats_{240};
    FrameStats::Summary summary_{};
    CpuMeter cpu_;
    double cpuPercent_{0.0};
    double residentMb_{0.0};
    double peakResidentMb_{0.0};
    double syncMs_{0.0};
    double syncAccum_{0.0};
    int syncCount_{0};
    QList<float> series_;
    QString graphicsApi_;
};

} // namespace rvn
