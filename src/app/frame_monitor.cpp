#include "frame_monitor.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScreen>

#include <algorithm>

namespace rvn {
namespace {

QString apiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::Software: return QStringLiteral("Software");
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D 12");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Null: return QStringLiteral("Null");
    default: return QStringLiteral("Unknown");
    }
}

constexpr int kSeriesLength = 180;

} // namespace

FrameMonitor::FrameMonitor(QObject* parent) : QObject(parent) {
    timer_.setInterval(250);
    connect(&timer_, &QTimer::timeout, this, &FrameMonitor::aggregate);
    timer_.start();
    series_.reserve(kSeriesLength);
}

void FrameMonitor::attach(QQuickWindow* window) {
    window_ = window;
    // Judge frames against the display's real refresh interval (120 Hz ProMotion is 8.3 ms).
    const auto retarget = [this] {
        if (window_ && window_->screen() && window_->screen()->refreshRate() > 1.0) {
            stats_.setTargetFrameMs(1000.0 / window_->screen()->refreshRate());
            emit targetChanged();
        }
    };
    retarget();
    connect(window, &QWindow::screenChanged, this, retarget);
    // All of these run on the render thread; DirectConnection keeps them there.
    connect(
        window, &QQuickWindow::beforeSynchronizing, this, [this] { syncStart_ = Clock::now(); }, Qt::DirectConnection);
    // Sync is the only phase where the GUI thread is blocked (updatePaintNode runs here), so it
    // is the render-side cost that can actually steal time from QML and input handling.
    connect(
        window, &QQuickWindow::afterSynchronizing, this,
        [this] { lastSyncMs_ = std::chrono::duration<float, std::milli>(Clock::now() - syncStart_).count(); },
        Qt::DirectConnection);
    connect(
        window, &QQuickWindow::frameSwapped, this,
        [this] {
            const auto now = Clock::now();
            if (lastSwap_ != Clock::time_point{}) {
                const float interval = std::chrono::duration<float, std::milli>(now - lastSwap_).count();
                (void)timings_.tryPush(FrameTiming{interval, lastSyncMs_}); // drop if GUI is far behind
            }
            lastSwap_ = now;
        },
        Qt::DirectConnection);
    connect(
        window, &QQuickWindow::sceneGraphInitialized, this,
        [this] {
            // Queued back to the GUI thread before touching graphicsApi_.
            QMetaObject::invokeMethod(this, [this] {
                if (window_) {
                    graphicsApi_ = apiName(window_->rendererInterface()->graphicsApi());
                    emit graphicsApiChanged();
                }
            });
        },
        Qt::DirectConnection);
}

void FrameMonitor::aggregate() {
    timings_.drain([this](const FrameTiming& t) {
        stats_.addFrame(static_cast<double>(t.intervalMs));
        syncAccum_ += static_cast<double>(t.syncMs);
        ++syncCount_;
        series_.append(t.intervalMs);
    });
    if (series_.size() > kSeriesLength) {
        series_.remove(0, series_.size() - kSeriesLength);
    }
    if (syncCount_ > 0) {
        syncMs_ = syncAccum_ / syncCount_;
        syncAccum_ = 0.0;
        syncCount_ = 0;
    }
    summary_ = stats_.summary();
    cpuPercent_ = cpu_.update();
    residentMb_ = static_cast<double>(sampleProcessUsage().residentBytes) / (1024.0 * 1024.0);
    peakResidentMb_ = std::max(peakResidentMb_, residentMb_);
    emit statsChanged();
}

void FrameMonitor::resetStats() {
    stats_.reset();
    summary_ = {};
    series_.clear();
    peakResidentMb_ = residentMb_;
    emit statsChanged();
}

QString FrameMonitor::summaryJson() const {
    const QJsonObject o{
        {QStringLiteral("graphics_api"), graphicsApi_},
        {QStringLiteral("target_frame_ms"), stats_.targetFrameMs()},
        {QStringLiteral("frames"), static_cast<double>(summary_.totalFrames)},
        {QStringLiteral("fps"), summary_.fps},
        {QStringLiteral("frame_ms_p50"), summary_.p50Ms},
        {QStringLiteral("frame_ms_p95"), summary_.p95Ms},
        {QStringLiteral("frame_ms_p99"), summary_.p99Ms},
        {QStringLiteral("frame_ms_max"), summary_.maxMs},
        {QStringLiteral("jank_frames_total"), static_cast<double>(summary_.totalJank)},
        {QStringLiteral("sync_ms_avg"), syncMs_},
        {QStringLiteral("cpu_percent"), cpuPercent_},
        {QStringLiteral("rss_mb"), residentMb_},
        {QStringLiteral("rss_mb_peak"), peakResidentMb_},
    };
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

} // namespace rvn
