#pragma once

#include "geometry.hpp"
#include "ring_history.hpp"
#include "telemetry.hpp"
#include "telemetry_worker.hpp"

#include <cstdint>
#include <optional>

namespace rvn {

/// UI-thread side of the pipeline. Called once per rendered frame: drains everything the
/// producer published since the last frame, keeps only what the frame needs (the newest
/// sample plus decimated histories), and tracks pipeline health.
///
/// This is the answer to "telemetry arrives faster than the display refreshes": at 1 kHz
/// telemetry and 60 Hz display, each frame consumes ~16 samples but renders one state.
/// Intermediate samples still feed the trail and the speed history, so nothing is lost
/// visually, but the UI does constant work per frame regardless of the input rate.
class TelemetryConsumer {
public:
    static constexpr std::size_t kTrailCapacity = 2048; ///< ~1 km of trail at 0.5 m spacing
    static constexpr std::size_t kSpeedCapacity = 600;  ///< 30 s at 20 Hz
    static constexpr double kTrailSpacingM = 0.5;
    static constexpr double kSpeedSamplePeriodS = 0.05;

    struct DrainStats {
        std::size_t consumed{};
        std::uint64_t newGaps{}; ///< samples missing from the sequence since the last drain
    };

    DrainStats drain(TelemetryWorker::SampleQueue& queue);
    void reset();

    [[nodiscard]] const std::optional<TelemetrySample>& latest() const noexcept { return latest_; }
    [[nodiscard]] const RingHistory<Vec2, kTrailCapacity>& trail() const noexcept { return trail_; }
    [[nodiscard]] const RingHistory<float, kSpeedCapacity>& speedHistory() const noexcept { return speed_; }

    [[nodiscard]] std::uint64_t totalConsumed() const noexcept { return totalConsumed_; }
    [[nodiscard]] std::uint64_t totalGaps() const noexcept { return totalGaps_; }
    [[nodiscard]] std::size_t maxBatch() const noexcept { return maxBatch_; }

    /// Seconds since the newest sample was produced, given "now" on the same steady clock.
    [[nodiscard]] double ageSeconds(std::int64_t nowNs) const noexcept;

private:
    void ingest(const TelemetrySample& s);

    std::optional<TelemetrySample> latest_;
    RingHistory<Vec2, kTrailCapacity> trail_;
    RingHistory<float, kSpeedCapacity> speed_;
    double lastSpeedSampleT_{-1.0};
    std::uint64_t expectedSeq_{0};
    bool haveSeq_{false};
    std::uint64_t totalConsumed_{0};
    std::uint64_t totalGaps_{0};
    std::uint64_t pendingGaps_{0};
    std::size_t maxBatch_{0};
};

} // namespace rvn
