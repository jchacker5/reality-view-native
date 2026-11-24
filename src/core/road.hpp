#pragma once

#include "geometry.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace rvn {

struct RoadPoint {
    Vec2 position{};
    double heading{};
    double curvature{};
};

struct RoadParams {
    double spacingM{1.0};
    double laneWidthM{3.7};
    int laneCount{3};
    double shoulderM{2.5};
};

struct RoadProjection {
    double s{};       ///< arc length of the closest centreline point
    double lateral{}; ///< signed distance from the centreline, + is left
    double heading{}; ///< road heading at s
    double distanceSq{};
};

/// Procedural, infinite, deterministic multi-lane highway.
///
/// The centreline's curvature is a sum of sinusoids whose amplitudes, wavelengths and phases
/// come from a seed. We generate the points with our own SplitMix64, not std::mt19937 with
/// std::uniform_real_distribution: the distributions are implementation-defined, so the
/// "same" seed would produce different roads on libc++ and libstdc++.
///
/// Thread-safety: not shared. The simulator thread and the render side each own a Road built
/// from the same seed, which gives both the identical road with zero synchronization.
class Road {
public:
    explicit Road(std::uint64_t seed, RoadParams params = {});

    /// Extend the centreline so that `at(s)` is valid. Amortized O(1) per metre.
    void ensureLength(double s);

    [[nodiscard]] double length() const noexcept;
    [[nodiscard]] RoadPoint at(double s) const noexcept; ///< linear interpolation, clamped
    [[nodiscard]] Vec2 pointAt(double s, double lateral) const noexcept;

    /// Local search within +/- windowM of a hint. O(window) instead of O(n): the car only
    /// moves a few cm per tick, so the previous projection is an excellent hint.
    [[nodiscard]] RoadProjection project(Vec2 p, double sHint, double windowM = 60.0) const noexcept;
    /// O(n) reference implementation, used by tests and benchmarks to validate project().
    [[nodiscard]] RoadProjection projectBruteForce(Vec2 p) const noexcept;

    /// Lateral offset of a lane centre from the road centreline. Lane 0 is the rightmost.
    [[nodiscard]] double laneCenterOffset(int lane) const noexcept;
    [[nodiscard]] int laneAt(double lateral) const noexcept;
    [[nodiscard]] double halfWidth() const noexcept;

    [[nodiscard]] const RoadParams& params() const noexcept { return params_; }
    [[nodiscard]] std::span<const RoadPoint> points() const noexcept { return points_; }

private:
    [[nodiscard]] double curvatureAt(double s) const noexcept;
    [[nodiscard]] RoadProjection projectRange(Vec2 p, std::size_t first, std::size_t last) const noexcept;

    struct Wave {
        double amplitude;
        double wavenumber;
        double phase;
    };
    RoadParams params_;
    std::array<Wave, 3> waves_{};
    std::vector<RoadPoint> points_;
};

} // namespace rvn
