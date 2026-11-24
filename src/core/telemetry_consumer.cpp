#include "telemetry_consumer.hpp"

#include <algorithm>

namespace rvn {

TelemetryConsumer::DrainStats TelemetryConsumer::drain(TelemetryWorker::SampleQueue& queue) {
    pendingGaps_ = 0;
    const std::size_t n = queue.drain([this](const TelemetrySample& s) { ingest(s); });
    totalConsumed_ += n;
    maxBatch_ = std::max(maxBatch_, n);
    return DrainStats{n, pendingGaps_};
}

void TelemetryConsumer::ingest(const TelemetrySample& s) {
    if (haveSeq_ && s.seq > expectedSeq_) {
        const std::uint64_t missing = s.seq - expectedSeq_;
        pendingGaps_ += missing;
        totalGaps_ += missing;
    }
    if (haveSeq_ && s.seq < expectedSeq_) {
        // Sequence restarted (new producer). Not a gap; start tracking afresh.
        trail_.clear();
        speed_.clear();
        lastSpeedSampleT_ = -1.0;
    }
    expectedSeq_ = s.seq + 1;
    haveSeq_ = true;

    // A big jump (reset, replay loop) breaks the trail rather than drawing a line across the map.
    const Vec2 p{s.x, s.y};
    if (!trail_.empty()) {
        const double d = length(p - trail_.back());
        if (d > 25.0) {
            trail_.clear();
        }
    }
    if (trail_.empty() || length(p - trail_.back()) >= kTrailSpacingM) {
        trail_.push(p);
    }

    if (s.simTimeS < lastSpeedSampleT_) {
        lastSpeedSampleT_ = -1.0; // time went backwards: replay looped or sim reset
    }
    if (lastSpeedSampleT_ < 0.0 || s.simTimeS - lastSpeedSampleT_ >= kSpeedSamplePeriodS) {
        speed_.push(s.speedMps);
        lastSpeedSampleT_ = s.simTimeS;
    }
    latest_ = s;
}

void TelemetryConsumer::reset() {
    *this = TelemetryConsumer{};
}

double TelemetryConsumer::ageSeconds(std::int64_t nowNs) const noexcept {
    if (!latest_) {
        return 1e9;
    }
    return static_cast<double>(nowNs - latest_->timestampNs) * 1e-9;
}

} // namespace rvn
