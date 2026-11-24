#pragma once

#include "frame_monitor.hpp"
#include "telemetry_worker.hpp"
#include "vehicle_view_model.hpp"

#include <QObject>
#include <QString>

#include <memory>

namespace rvn {

struct AppOptions {
    std::uint64_t seed{7};
    int publishRateHz{240};
    QString replayPath; ///< empty = live simulator
};

/// Composition root: builds the object graph in dependency order and tears it down in reverse.
/// Destruction order matters: the worker thread must stop before the view model that reads
/// its queue goes away, so the worker is declared first (destroyed last) and stopped
/// explicitly in the destructor.
class AppContext : public QObject {
    Q_OBJECT
public:
    /// Throws std::runtime_error if a replay file cannot be read or parsed.
    explicit AppContext(const AppOptions& options, QObject* parent = nullptr);
    ~AppContext() override;

    [[nodiscard]] VehicleViewModel* vehicle() const { return vehicle_.get(); }
    [[nodiscard]] FrameMonitor* monitor() const { return monitor_.get(); }
    [[nodiscard]] TelemetryWorker& worker() const { return *worker_; }

private:
    std::unique_ptr<TelemetryWorker> worker_;
    std::unique_ptr<VehicleViewModel> vehicle_;
    std::unique_ptr<FrameMonitor> monitor_;
};

} // namespace rvn
