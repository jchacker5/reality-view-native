#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rvn {

/// Rolling frame-time statistics. Averages hide stutter, so we report tail percentiles and
/// count "janky" frames (longer than 1.5x the vsync interval, i.e. a visibly missed frame).
class FrameStats {
public:
    struct Summary {
        double fps{}; ///< frames / wall time across the window
        double meanMs{};
        double p50Ms{};
        double p95Ms{};
        double p99Ms{};
        double maxMs{};
        std::size_t windowJank{};
        std::uint64_t totalJank{};
        std::uint64_t totalFrames{};
    };

    explicit FrameStats(std::size_t window = 240, double targetFrameMs = 1000.0 / 60.0);

    void addFrame(double frameMs);
    /// O(n) per call (nth_element on a scratch copy). Called a few times per second, not per frame.
    [[nodiscard]] Summary summary() const;
    void reset();
    /// Retarget (e.g. 120 Hz displays). Clears the window, since old jank counts used the old threshold.
    void setTargetFrameMs(double ms);

    [[nodiscard]] double targetFrameMs() const noexcept { return targetMs_; }
    [[nodiscard]] double jankThresholdMs() const noexcept { return 1.5 * targetMs_; }

private:
    std::vector<double> ring_;
    std::size_t next_{0};
    std::size_t count_{0};
    double targetMs_;
    std::uint64_t totalJank_{0};
    std::uint64_t totalFrames_{0};
    mutable std::vector<double> scratch_;
};

} // namespace rvn
