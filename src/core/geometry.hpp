#pragma once

#include <cmath>
#include <numbers>

namespace rvn {

struct Vec2 {
    double x{};
    double y{};

    constexpr Vec2 operator+(Vec2 o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const noexcept { return {x * s, y * s}; }
    constexpr bool operator==(const Vec2&) const noexcept = default;
};

constexpr double dot(Vec2 a, Vec2 b) noexcept {
    return a.x * b.x + a.y * b.y;
}
/// z component of the 3D cross product; > 0 when b is counter-clockwise (left) of a.
constexpr double cross(Vec2 a, Vec2 b) noexcept {
    return a.x * b.y - a.y * b.x;
}
inline double length(Vec2 v) noexcept {
    return std::hypot(v.x, v.y);
}
inline Vec2 unitFromHeading(double heading) noexcept {
    return {std::cos(heading), std::sin(heading)};
}
/// Left-hand normal of a heading (90 degrees counter-clockwise).
inline Vec2 leftNormal(double heading) noexcept {
    return {-std::sin(heading), std::cos(heading)};
}

/// Wrap an angle into [-pi, pi).
inline double wrapAngle(double a) noexcept {
    constexpr double kTwoPi = 2.0 * std::numbers::pi;
    a = std::fmod(a + std::numbers::pi, kTwoPi);
    if (a < 0) {
        a += kTwoPi;
    }
    return a - std::numbers::pi;
}

/// A rigid 2D transform: position and heading of a frame expressed in the world frame.
struct Pose2 {
    Vec2 position{};
    double heading{};

    /// Vehicle/local frame -> world frame.
    [[nodiscard]] Vec2 toWorld(Vec2 local) const noexcept {
        const double c = std::cos(heading);
        const double s = std::sin(heading);
        return {position.x + c * local.x - s * local.y, position.y + s * local.x + c * local.y};
    }

    /// World frame -> local frame (+x forward, +y left). Inverse of toWorld.
    [[nodiscard]] Vec2 toLocal(Vec2 world) const noexcept {
        const Vec2 d = world - position;
        const double c = std::cos(heading);
        const double s = std::sin(heading);
        return {c * d.x + s * d.y, -s * d.x + c * d.y};
    }
};

/// Local tangent-plane (east/north metres) -> WGS-84 latitude/longitude, equirectangular
/// approximation. Error is well under a metre within a few km of the origin, which is the
/// regime a single drive lives in; a full ECEF conversion would be overkill here.
struct GeoOrigin {
    double latitudeDeg{37.4947}; // Fremont, CA
    double longitudeDeg{-121.9446};
};

struct LatLon {
    double latitudeDeg{};
    double longitudeDeg{};
};

inline LatLon enuToLatLon(const GeoOrigin& origin, Vec2 enu) noexcept {
    constexpr double kEarthRadiusM = 6'378'137.0;
    constexpr double kRadToDeg = 180.0 / std::numbers::pi;
    const double lat0 = origin.latitudeDeg / kRadToDeg;
    return {origin.latitudeDeg + (enu.y / kEarthRadiusM) * kRadToDeg,
            origin.longitudeDeg + (enu.x / (kEarthRadiusM * std::cos(lat0))) * kRadToDeg};
}

} // namespace rvn
