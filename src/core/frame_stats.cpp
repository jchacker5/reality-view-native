#include "frame_stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace rvn {

FrameStats::FrameStats(std::size_t window, double targetFrameMs)
    : ring_(std::max<std::size_t>(window, 1)), targetMs_(targetFrameMs) {
    scratch_.reserve(ring_.size());
}

void FrameStats::addFrame(double frameMs) {
    ring_[next_] = frameMs;
    next_ = (next_ + 1) % ring_.size();
    count_ = std::min(count_ + 1, ring_.size());
    ++totalFrames_;
    if (frameMs > jankThresholdMs()) {
        ++totalJank_;
    }
}

FrameStats::Summary FrameStats::summary() const {
    Summary s;
    s.totalJank = totalJank_;
    s.totalFrames = totalFrames_;
    if (count_ == 0) {
        return s;
    }
    scratch_.assign(ring_.begin(), ring_.begin() + static_cast<std::ptrdiff_t>(count_));
    const double sum = std::accumulate(scratch_.begin(), scratch_.end(), 0.0);
    s.meanMs = sum / static_cast<double>(count_);
    s.fps = sum > 0.0 ? 1000.0 * static_cast<double>(count_) / sum : 0.0;
    s.maxMs = *std::max_element(scratch_.begin(), scratch_.end());
    s.windowJank = static_cast<std::size_t>(
        std::count_if(scratch_.begin(), scratch_.end(), [this](double ms) { return ms > jankThresholdMs(); }));

    // Nearest-rank percentiles via nth_element: O(n) each instead of a full O(n log n) sort.
    const auto percentile = [this](double p) {
        const auto rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(scratch_.size()))) - 1;
        const auto idx = std::min(rank, scratch_.size() - 1);
        std::nth_element(scratch_.begin(), scratch_.begin() + static_cast<std::ptrdiff_t>(idx), scratch_.end());
        return scratch_[idx];
    };
    s.p50Ms = percentile(0.50);
    s.p95Ms = percentile(0.95);
    s.p99Ms = percentile(0.99);
    return s;
}

void FrameStats::reset() {
    next_ = 0;
    count_ = 0;
    totalJank_ = 0;
    totalFrames_ = 0;
}

void FrameStats::setTargetFrameMs(double ms) {
    if (ms > 0.0) {
        targetMs_ = ms;
        reset();
    }
}

} // namespace rvn
