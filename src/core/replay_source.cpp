#include "replay_source.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rvn {

ReplaySource::ReplaySource(TelemetryLog log) : log_(std::move(log)) {
    if (log_.samples.size() < 2) {
        throw std::invalid_argument("replay needs at least two samples");
    }
}

double ReplaySource::duration() const noexcept {
    return log_.samples.back().simTimeS - log_.samples.front().simTimeS;
}

void ReplaySource::apply(const Command& command) {
    if (std::holds_alternative<cmd::Reset>(command)) {
        playhead_ = 0.0;
    }
    // Everything else is ignored: a recording cannot be steered.
}

void ReplaySource::step(double dt) {
    const double d = duration();
    playhead_ = d > 0.0 ? std::fmod(playhead_ + dt, d) : 0.0;
}

TelemetrySample ReplaySource::sample() const {
    const auto& rows = log_.samples;
    const double t = rows.front().simTimeS + playhead_;
    // First row strictly after t; its predecessor is the row we are in.
    const auto upper = std::upper_bound(rows.begin(), rows.end(), t,
                                        [](double value, const TelemetrySample& s) { return value < s.simTimeS; });
    if (upper == rows.begin()) {
        return rows.front();
    }
    if (upper == rows.end()) {
        return rows.back();
    }
    const TelemetrySample& a = *(upper - 1);
    const TelemetrySample& b = *upper;
    const double span = b.simTimeS - a.simTimeS;
    const double u = span > 0.0 ? (t - a.simTimeS) / span : 0.0;
    const auto lerp = [u](double x, double y) { return x + (y - x) * u; };
    const auto lerpf = [u](float x, float y) { return static_cast<float>(x + (y - x) * static_cast<float>(u)); };

    TelemetrySample out = a; // discrete fields (gear, signals, lane) hold the previous value
    out.simTimeS = t;
    out.x = lerp(a.x, b.x);
    out.y = lerp(a.y, b.y);
    out.latitude = lerp(a.latitude, b.latitude);
    out.longitude = lerp(a.longitude, b.longitude);
    // Interpolate heading along the short way round the circle.
    out.heading = static_cast<float>(
        wrapAngle(static_cast<double>(a.heading) + wrapAngle(static_cast<double>(b.heading - a.heading)) * u));
    out.speedMps = lerpf(a.speedMps, b.speedMps);
    out.accelMps2 = lerpf(a.accelMps2, b.accelMps2);
    out.yawRateRadS = lerpf(a.yawRateRadS, b.yawRateRadS);
    out.steeringWheelDeg = lerpf(a.steeringWheelDeg, b.steeringWheelDeg);
    out.laneOffsetM = lerpf(a.laneOffsetM, b.laneOffsetM);
    out.roadS = lerpf(a.roadS, b.roadS);
    out.objectCount = 0;
    return out;
}

} // namespace rvn
