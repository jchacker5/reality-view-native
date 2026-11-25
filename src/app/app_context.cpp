#include "app_context.hpp"

#include "can_bus.hpp"
#include "can_source.hpp"
#include "dbc.hpp"
#include "drive_simulator.hpp"
#include "replay_source.hpp"
#include "telemetry_csv.hpp"

#include <fstream>
#include <stdexcept>

namespace rvn {

AppContext::AppContext(const AppOptions& options, QObject* parent) : QObject(parent) {
    std::unique_ptr<TelemetrySource> source;
    const bool replay = !options.replayPath.isEmpty();
    const bool canLive = !options.canInterface.isEmpty();
    const bool canLog = !options.canLogPath.isEmpty();
    const bool externalSource = replay || canLive || canLog;

    if (replay) {
        std::ifstream in(options.replayPath.toStdString());
        if (!in) {
            throw std::runtime_error("cannot open replay file: " + options.replayPath.toStdString());
        }
        source = std::make_unique<ReplaySource>(readTelemetryCsv(in));
    } else if (canLive || canLog) {
        if (options.dbcPath.isEmpty()) {
            throw std::runtime_error("--dbc <file.dbc> is required when using --can or --can-log");
        }
        std::ifstream dbcIn(options.dbcPath.toStdString());
        if (!dbcIn) {
            throw std::runtime_error("cannot open DBC file: " + options.dbcPath.toStdString());
        }
        DbcDatabase db = DbcDatabase::parse(dbcIn);
        if (canLog) {
            std::ifstream logIn(options.canLogPath.toStdString());
            if (!logIn) {
                throw std::runtime_error("cannot open CAN log: " + options.canLogPath.toStdString());
            }
            source = std::make_unique<CanTelemetrySource>(std::move(db), readCandumpLog(logIn), options.seed);
        } else {
            source = std::make_unique<CanTelemetrySource>(
                std::move(db), openSocketCan(options.canInterface.toStdString()), options.seed);
        }
    } else {
        SimConfig cfg;
        cfg.seed = options.seed;
        source = std::make_unique<DriveSimulator>(cfg);
    }
    worker_ = std::make_unique<TelemetryWorker>(std::move(source), options.publishRateHz);
    vehicle_ = std::make_unique<VehicleViewModel>(*worker_, externalSource);
    monitor_ = std::make_unique<FrameMonitor>();
}

void AppContext::start() {
    worker_->start();
}

AppContext::~AppContext() {
    worker_->stop(); // join before anything that references the worker is destroyed
}

} // namespace rvn
