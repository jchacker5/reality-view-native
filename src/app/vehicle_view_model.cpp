#include "vehicle_view_model.hpp"

#include <QHash>
#include <QLoggingCategory>

#include <algorithm>

#include <chrono>
#include <cmath>
#include <numbers>

Q_LOGGING_CATEGORY(lcPipeline, "rvn.pipeline")

namespace rvn {
namespace {

std::int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

VehicleViewModel::VehicleViewModel(TelemetryWorker& worker, bool replay, QObject* parent)
    : QObject(parent), worker_(worker), replay_(replay) {
    pipelineClock_.start();
    seriesClock_.start();
    speedSeries_.reserve(static_cast<qsizetype>(TelemetryConsumer::kSpeedCapacity));
}

int VehicleViewModel::tick() {
    QElapsedTimer t;
    t.start();
    const auto stats = consumer_.drain(worker_.samples());
    drainMicrosAccum_ += static_cast<double>(t.nsecsElapsed()) / 1000.0;
    ++framesSincePublish_;

    if (stats.newGaps > 0) {
        qCDebug(lcPipeline) << "sequence gap:" << stats.newGaps << "samples missing";
    }
    if (stats.consumed > 0) {
        if (const auto& s = consumer_.latest()) {
            const Vec2 p{s->x, s->y};
            if (lastPosition_) {
                const double d = length(p - *lastPosition_);
                if (d < 25.0) { // ignore teleports (reset, replay loop)
                    odometerM_ += d;
                }
            }
            lastPosition_ = p;
        }
        emit telemetryChanged();
    }

    if (seriesClock_.elapsed() >= 100) {
        seriesClock_.restart();
        speedSeries_.clear();
        const auto& h = consumer_.speedHistory();
        for (std::size_t i = 0; i < h.size(); ++i) {
            speedSeries_.append(static_cast<float>(static_cast<double>(h[i]) * kMpsToMph));
        }
        emit seriesChanged();
    }
    if (pipelineClock_.elapsed() >= 250) {
        publishPipelineStats();
    }
    return static_cast<int>(stats.consumed);
}

void VehicleViewModel::publishPipelineStats() {
    const double seconds = static_cast<double>(pipelineClock_.restart()) / 1000.0;
    const std::uint64_t consumed = consumer_.totalConsumed();
    const auto delta = static_cast<double>(consumed - consumedAtLastPublish_);
    consumedAtLastPublish_ = consumed;
    measuredRateHz_ = seconds > 0.0 ? delta / seconds : 0.0;
    samplesPerFrame_ = framesSincePublish_ > 0 ? delta / static_cast<double>(framesSincePublish_) : 0.0;
    drainMicros_ = framesSincePublish_ > 0 ? drainMicrosAccum_ / static_cast<double>(framesSincePublish_) : 0.0;
    drainMicrosAccum_ = 0.0;
    framesSincePublish_ = 0;
    dataAgeMs_ = consumer_.ageSeconds(steadyNowNs()) * 1000.0;
    emit pipelineChanged();
}

void VehicleViewModel::post(const Command& c) {
    if (!worker_.post(c)) {
        qCWarning(lcPipeline) << "command queue full; command dropped";
    }
}

void VehicleViewModel::setManualInput(double throttle, double brake, double steer) {
    post(cmd::ManualInput{static_cast<float>(throttle), static_cast<float>(brake), static_cast<float>(steer)});
}

void VehicleViewModel::shiftGear(const QString& g) {
    static const QHash<QString, Gear> kGears{
        {QStringLiteral("P"), Gear::Park},
        {QStringLiteral("R"), Gear::Reverse},
        {QStringLiteral("N"), Gear::Neutral},
        {QStringLiteral("D"), Gear::Drive},
    };
    if (const auto it = kGears.constFind(g); it != kGears.cend()) {
        post(cmd::ShiftGear{*it});
    }
}

void VehicleViewModel::signalTurn(int signal) {
    if (signal < 0 || signal > 2) {
        return;
    }
    post(cmd::SignalTurn{static_cast<TurnSignal>(signal)});
}

void VehicleViewModel::toggleAutopilot() {
    post(cmd::SetAutopilot{autopilotState() != static_cast<int>(AutopilotState::Active)});
}

void VehicleViewModel::adjustCruise(double deltaMph) {
    const double target = std::clamp(setSpeedMph() + deltaMph, 0.0, 85.0);
    post(cmd::SetCruiseSpeed{static_cast<float>(target / kMpsToMph)});
}

void VehicleViewModel::setPublishRate(int hz) {
    post(cmd::SetPublishRate{hz});
}
void VehicleViewModel::injectStall(int milliseconds) {
    post(cmd::InjectStall{milliseconds});
}

void VehicleViewModel::setDropout(bool enabled) {
    dropout_ = enabled;
    post(cmd::SetDropout{enabled});
    emit pipelineChanged();
}

void VehicleViewModel::reset() {
    post(cmd::Reset{});
    odometerM_ = 0.0;
    lastPosition_.reset();
}

// ---- Property getters --------------------------------------------------------------------
// Each reads the newest sample; with no data yet they return neutral values.

#define RVN_SAMPLE_OR(expr, fallback)                                                                                  \
    const auto& s = consumer_.latest();                                                                                \
    return s ? (expr) : (fallback)

double VehicleViewModel::speedMph() const {
    RVN_SAMPLE_OR(std::abs(static_cast<double>(s->speedMps)) * kMpsToMph, 0.0);
}
double VehicleViewModel::accelMps2() const {
    RVN_SAMPLE_OR(static_cast<double>(s->accelMps2), 0.0);
}
QString VehicleViewModel::gear() const {
    RVN_SAMPLE_OR(QString::fromLatin1(label(s->gear).data(), static_cast<qsizetype>(label(s->gear).size())),
                  QStringLiteral("P"));
}
double VehicleViewModel::steeringWheelDeg() const {
    RVN_SAMPLE_OR(static_cast<double>(s->steeringWheelDeg), 0.0);
}
double VehicleViewModel::throttle() const {
    RVN_SAMPLE_OR(static_cast<double>(s->throttle), 0.0);
}
double VehicleViewModel::brake() const {
    RVN_SAMPLE_OR(static_cast<double>(s->brake), 0.0);
}
int VehicleViewModel::turnSignal() const {
    RVN_SAMPLE_OR(static_cast<int>(s->turnSignal), 0);
}
int VehicleViewModel::autopilotState() const {
    RVN_SAMPLE_OR(static_cast<int>(s->autopilot), 0);
}
QString VehicleViewModel::autopilotText() const {
    RVN_SAMPLE_OR(QString::fromLatin1(label(s->autopilot).data(), static_cast<qsizetype>(label(s->autopilot).size())),
                  QStringLiteral("Unavailable"));
}
double VehicleViewModel::setSpeedMph() const {
    RVN_SAMPLE_OR(static_cast<double>(s->setSpeedMps) * kMpsToMph, 0.0);
}
int VehicleViewModel::lane() const {
    RVN_SAMPLE_OR(static_cast<int>(s->lane), 0);
}
double VehicleViewModel::laneOffsetM() const {
    RVN_SAMPLE_OR(static_cast<double>(s->laneOffsetM), 0.0);
}
double VehicleViewModel::latitude() const {
    RVN_SAMPLE_OR(s->latitude, 0.0);
}
double VehicleViewModel::longitude() const {
    RVN_SAMPLE_OR(s->longitude, 0.0);
}
double VehicleViewModel::headingDeg() const {
    RVN_SAMPLE_OR(std::fmod(450.0 - static_cast<double>(s->heading) * 180.0 / std::numbers::pi, 360.0), 0.0);
}
int VehicleViewModel::objectCount() const {
    RVN_SAMPLE_OR(static_cast<int>(s->objectCount), 0);
}
double VehicleViewModel::simTime() const {
    RVN_SAMPLE_OR(s->simTimeS, 0.0);
}

#undef RVN_SAMPLE_OR

} // namespace rvn
