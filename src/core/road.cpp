#include "road.hpp"

#include "random.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace rvn {

Road::Road(std::uint64_t seed, RoadParams params) : params_(params) {
    SplitMix64 rng{seed};
    for (auto& w : waves_) {
        // Peak combined curvature ~0.004 1/m (R ~ 250 m): comfortable at highway speed.
        w.amplitude = rng.uniform(0.0006, 0.0013);
        w.wavenumber = 2.0 * std::numbers::pi / rng.uniform(350.0, 1600.0);
        w.phase = rng.uniform(0.0, 2.0 * std::numbers::pi);
    }
    points_.reserve(4096);
    points_.push_back(RoadPoint{{0.0, 0.0}, 0.0, curvatureAt(0.0)});
    ensureLength(1000.0);
}

double Road::curvatureAt(double s) const noexcept {
    // Fade curvature in over the first 200 m so every drive starts on a straight.
    const double fade = std::clamp(s / 200.0, 0.0, 1.0);
    double k = 0.0;
    for (const auto& w : waves_) {
        k += w.amplitude * std::sin(w.wavenumber * s + w.phase);
    }
    return k * fade;
}

void Road::ensureLength(double s) {
    const double ds = params_.spacingM;
    while (length() < s) {
        const RoadPoint& last = points_.back();
        const double sNext = static_cast<double>(points_.size()) * ds;
        const double k = curvatureAt(sNext);
        // Midpoint (RK2) integration of heading and position.
        const double kMid = 0.5 * (last.curvature + k);
        const double heading = last.heading + kMid * ds;
        const double headingMid = last.heading + 0.5 * kMid * ds;
        const Vec2 pos = last.position + unitFromHeading(headingMid) * ds;
        points_.push_back(RoadPoint{pos, heading, k});
    }
}

double Road::length() const noexcept {
    return static_cast<double>(points_.size() - 1) * params_.spacingM;
}

RoadPoint Road::at(double s) const noexcept {
    const double u = std::clamp(s / params_.spacingM, 0.0, static_cast<double>(points_.size() - 1));
    const auto i = static_cast<std::size_t>(std::floor(u));
    if (i + 1 >= points_.size()) {
        return points_.back();
    }
    const double t = u - static_cast<double>(i);
    const RoadPoint& a = points_[i];
    const RoadPoint& b = points_[i + 1];
    return RoadPoint{a.position + (b.position - a.position) * t, a.heading + (b.heading - a.heading) * t,
                     a.curvature + (b.curvature - a.curvature) * t};
}

Vec2 Road::pointAt(double s, double lateral) const noexcept {
    const RoadPoint rp = at(s);
    return rp.position + leftNormal(rp.heading) * lateral;
}

RoadProjection Road::projectRange(Vec2 p, std::size_t first, std::size_t last) const noexcept {
    RoadProjection best{};
    best.distanceSq = std::numeric_limits<double>::max();
    for (std::size_t i = first; i < last; ++i) {
        const Vec2 a = points_[i].position;
        const Vec2 ab = points_[i + 1].position - a;
        const double lenSq = dot(ab, ab);
        const double t = std::clamp(dot(p - a, ab) / lenSq, 0.0, 1.0);
        const Vec2 closest = a + ab * t;
        const Vec2 d = p - closest;
        const double distSq = dot(d, d);
        if (distSq < best.distanceSq) {
            best.distanceSq = distSq;
            best.s = (static_cast<double>(i) + t) * params_.spacingM;
            best.heading = points_[i].heading + (points_[i + 1].heading - points_[i].heading) * t;
            best.lateral = cross(ab, p - a) / std::sqrt(lenSq);
        }
    }
    return best;
}

RoadProjection Road::project(Vec2 p, double sHint, double windowM) const noexcept {
    const double maxIndex = static_cast<double>(points_.size() - 1);
    const double lo = std::clamp((sHint - windowM) / params_.spacingM, 0.0, maxIndex);
    const double hi = std::clamp((sHint + windowM) / params_.spacingM, 0.0, maxIndex);
    const auto first = static_cast<std::size_t>(lo);
    const auto last = static_cast<std::size_t>(std::ceil(hi));
    if (last <= first) {
        return projectBruteForce(p);
    }
    return projectRange(p, first, last);
}

RoadProjection Road::projectBruteForce(Vec2 p) const noexcept {
    return projectRange(p, 0, points_.size() - 1);
}

double Road::laneCenterOffset(int lane) const noexcept {
    return (static_cast<double>(lane) - 0.5 * static_cast<double>(params_.laneCount - 1)) * params_.laneWidthM;
}

int Road::laneAt(double lateral) const noexcept {
    const double fromRightEdge = lateral + 0.5 * static_cast<double>(params_.laneCount) * params_.laneWidthM;
    const int lane = static_cast<int>(std::floor(fromRightEdge / params_.laneWidthM));
    return std::clamp(lane, 0, params_.laneCount - 1);
}

double Road::halfWidth() const noexcept {
    return 0.5 * static_cast<double>(params_.laneCount) * params_.laneWidthM;
}

} // namespace rvn
