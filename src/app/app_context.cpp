#include "app_context.hpp"

#include "drive_simulator.hpp"
#include "replay_source.hpp"
#include "telemetry_csv.hpp"

#include <fstream>
#include <stdexcept>

namespace rvn {

AppContext::AppContext(const AppOptions& options, QObject* parent) : QObject(parent) {
    std::unique_ptr<TelemetrySource> source;
    const bool replay = !options.replayPath.isEmpty();
    if (replay) {
        std::ifstream in(options.replayPath.toStdString());
        if (!in) {
            throw std::runtime_error("cannot open replay file: " + options.replayPath.toStdString());
        }
        source = std::make_unique<ReplaySource>(readTelemetryCsv(in));
    } else {
        SimConfig cfg;
        cfg.seed = options.seed;
        source = std::make_unique<DriveSimulator>(cfg);
    }
    worker_ = std::make_unique<TelemetryWorker>(std::move(source), options.publishRateHz);
    vehicle_ = std::make_unique<VehicleViewModel>(*worker_, replay);
    monitor_ = std::make_unique<FrameMonitor>();
}

void AppContext::start() {
    worker_->start();
}

AppContext::~AppContext() {
    worker_->stop(); // join before anything that references the worker is destroyed
}

} // namespace rvn
