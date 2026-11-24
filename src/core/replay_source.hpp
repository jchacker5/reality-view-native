#pragma once

#include "telemetry_csv.hpp"
#include "telemetry_source.hpp"

namespace rvn {

/// Replays a recorded TelemetryLog in real time, interpolating between rows and looping at
/// the end. Lookup is a binary search (O(log n)) from the playhead; Reset rewinds.
class ReplaySource final : public TelemetrySource {
public:
    /// Throws std::invalid_argument for a log with fewer than two samples.
    explicit ReplaySource(TelemetryLog log);

    void apply(const Command& command) override;
    void step(double dt) override;
    [[nodiscard]] TelemetrySample sample() const override;
    [[nodiscard]] std::uint64_t roadSeed() const override { return log_.roadSeed; }

    [[nodiscard]] double playhead() const noexcept { return playhead_; }
    [[nodiscard]] double duration() const noexcept;

private:
    TelemetryLog log_;
    double playhead_{}; ///< seconds since the first sample
};

} // namespace rvn
