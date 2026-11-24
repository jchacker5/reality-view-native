#include "scene_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace rvn {
namespace {

constexpr Rgba kAsphalt{38, 42, 48, 255};
constexpr Rgba kShoulder{26, 29, 33, 255};
constexpr Rgba kEdgeLine{200, 205, 212, 255};
constexpr Rgba kLaneDash{150, 156, 165, 255};
constexpr double kLineWidth = 0.15;
constexpr double kDashLength = 3.0;
constexpr double kDashPeriod = 12.0;

/// Emit a strip of quads between two lateral offsets along the road, optionally dashed.
void lateralStrip(MeshBuilder& mb, const Road& road, double sFrom, double sTo, double latA, double latB, Rgba color,
                  bool dashed) {
    const double ds = road.params().spacingM;
    double s = sFrom;
    while (s < sTo) {
        double sNext = std::min(s + ds, sTo);
        if (dashed) {
            const double phase = std::fmod(s, kDashPeriod);
            if (phase >= kDashLength) {
                // Skip ahead to the start of the next dash.
                s += kDashPeriod - phase;
                continue;
            }
            sNext = std::min(sNext, s + (kDashLength - phase));
        }
        const RoadPoint p0 = road.at(s);
        const RoadPoint p1 = road.at(sNext);
        const Vec2 n0 = leftNormal(p0.heading);
        const Vec2 n1 = leftNormal(p1.heading);
        mb.quad(p0.position + n0 * latA, p1.position + n1 * latA, p1.position + n1 * latB, p0.position + n0 * latB,
                color);
        s = sNext;
    }
}

} // namespace

void MeshBuilder::vertex(Vec2 p, Rgba c) {
    out_.push_back(
        ColoredVertex{static_cast<float>(p.x - origin_.x), static_cast<float>(p.y - origin_.y), c.r, c.g, c.b, c.a});
}

void MeshBuilder::triangle(Vec2 a, Vec2 b, Vec2 c, Rgba color) {
    vertex(a, color);
    vertex(b, color);
    vertex(c, color);
}

void MeshBuilder::quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Rgba color) {
    triangle(a, b, c, color);
    triangle(a, c, d, color);
}

void MeshBuilder::convexPolygon(std::span<const Vec2> points, Rgba color) {
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        triangle(points[0], points[i], points[i + 1], color);
    }
}

void buildRoadMesh(const Road& road, double sFrom, double sTo, Vec2 origin, std::vector<ColoredVertex>& out) {
    sFrom = std::max(sFrom, 0.0);
    sTo = std::min(sTo, road.length());
    if (sTo <= sFrom) {
        return;
    }
    MeshBuilder mb(out, origin);
    const double half = road.halfWidth();
    const RoadParams& rp = road.params();

    lateralStrip(mb, road, sFrom, sTo, -half - rp.shoulderM, -half, kShoulder, false);
    lateralStrip(mb, road, sFrom, sTo, half, half + rp.shoulderM, kShoulder, false);
    lateralStrip(mb, road, sFrom, sTo, -half, half, kAsphalt, false);
    // Solid edge lines.
    lateralStrip(mb, road, sFrom, sTo, -half, -half + kLineWidth, kEdgeLine, false);
    lateralStrip(mb, road, sFrom, sTo, half - kLineWidth, half, kEdgeLine, false);
    // Dashed dividers between lanes.
    for (int lane = 0; lane + 1 < rp.laneCount; ++lane) {
        const double boundary = road.laneCenterOffset(lane) + 0.5 * rp.laneWidthM;
        lateralStrip(mb, road, sFrom, sTo, boundary - 0.5 * kLineWidth, boundary + 0.5 * kLineWidth, kLaneDash, true);
    }
}

void buildPlannedPathMesh(const Road& road, double sFrom, double lengthM, double lateral, Vec2 origin, Rgba color,
                          std::vector<ColoredVertex>& out) {
    MeshBuilder mb(out, origin);
    constexpr double kHalfWidth = 0.9;
    constexpr double kStep = 2.0;
    const double sEnd = std::min(sFrom + lengthM, road.length());
    for (double s = sFrom; s < sEnd; s += kStep) {
        const double s1 = std::min(s + kStep, sEnd);
        // Fade out toward the far end.
        const double fade = 1.0 - (s - sFrom) / lengthM;
        const Rgba c = Rgba::premultiplied(color.r, color.g, color.b, (color.a / 255.0) * fade);
        const RoadPoint p0 = road.at(s);
        const RoadPoint p1 = road.at(s1);
        const Vec2 c0 = p0.position + leftNormal(p0.heading) * lateral;
        const Vec2 c1 = p1.position + leftNormal(p1.heading) * lateral;
        const Vec2 n0 = leftNormal(p0.heading) * kHalfWidth;
        const Vec2 n1 = leftNormal(p1.heading) * kHalfWidth;
        mb.quad(c0 - n0, c1 - n1, c1 + n1, c0 + n0, c);
    }
}

void buildTrailMesh(std::span<const Vec2> points, double widthM, Vec2 origin, std::vector<ColoredVertex>& out) {
    if (points.size() < 2) {
        return;
    }
    MeshBuilder mb(out, origin);
    const double half = 0.5 * widthM;
    const auto normalAt = [&](std::size_t i) {
        const Vec2 a = points[i == 0 ? 0 : i - 1];
        const Vec2 b = points[std::min(i + 1, points.size() - 1)];
        const Vec2 d = b - a;
        const double len = length(d);
        return len > 1e-9 ? Vec2{-d.y / len, d.x / len} : Vec2{0.0, 0.0};
    };
    const double n = static_cast<double>(points.size() - 1);
    Vec2 prevNormal = normalAt(0);
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const Vec2 nextNormal = normalAt(i + 1);
        const double alpha = 0.55 * (static_cast<double>(i) / n); // oldest end fades to transparent
        const Rgba c = Rgba::premultiplied(61, 123, 255, alpha);
        mb.quad(points[i] - prevNormal * half, points[i + 1] - nextNormal * half, points[i + 1] + nextNormal * half,
                points[i] + prevNormal * half, c);
        prevNormal = nextNormal;
    }
}

void appendVehicleMesh(MeshBuilder& mb, const Pose2& pose, const VehicleAppearance& look) {
    const double hl = 0.5 * look.length;
    const double hw = 0.5 * look.width;
    const double chamfer = 0.35;
    const auto w = [&pose](double x, double y) { return pose.toWorld({x, y}); };

    // Wheels first so the body overlaps their inner halves.
    const double wheelL = 0.70;
    const double wheelW = 0.26;
    const double axleFront = hl - 0.95;
    const double axleRear = -hl + 0.95;
    const Rgba tyre{14, 15, 17, 255};
    const auto wheel = [&](double cx, double cy, double angle) {
        const Pose2 wp{pose.toWorld({cx, cy}), pose.heading + angle};
        mb.quad(wp.toWorld({-wheelL / 2, -wheelW / 2}), wp.toWorld({wheelL / 2, -wheelW / 2}),
                wp.toWorld({wheelL / 2, wheelW / 2}), wp.toWorld({-wheelL / 2, wheelW / 2}), tyre);
    };
    wheel(axleFront, hw - 0.12, look.roadWheelAngle);
    wheel(axleFront, -hw + 0.12, look.roadWheelAngle);
    wheel(axleRear, hw - 0.12, 0.0);
    wheel(axleRear, -hw + 0.12, 0.0);

    const std::array<Vec2, 8> body{w(hl, hw - chamfer),        w(hl - chamfer, hw),         w(-hl + chamfer * 0.6, hw),
                                   w(-hl, hw - chamfer * 0.6), w(-hl, -hw + chamfer * 0.6), w(-hl + chamfer * 0.6, -hw),
                                   w(hl - chamfer, -hw),       w(hl, -hw + chamfer)};
    mb.convexPolygon(body, look.body);

    // Glass canopy: windshield to rear window as one dark trapezoid, roof slightly lighter.
    const std::array<Vec2, 4> glass{w(hl * 0.42, hw * 0.78), w(-hl * 0.70, hw * 0.80), w(-hl * 0.70, -hw * 0.80),
                                    w(hl * 0.42, -hw * 0.78)};
    mb.convexPolygon(glass, look.glass);
    const Rgba roof{static_cast<std::uint8_t>((look.glass.r + look.body.r) / 2),
                    static_cast<std::uint8_t>((look.glass.g + look.body.g) / 2),
                    static_cast<std::uint8_t>((look.glass.b + look.body.b) / 2), 255};
    const std::array<Vec2, 4> roofPanel{w(hl * 0.12, hw * 0.66), w(-hl * 0.45, hw * 0.68), w(-hl * 0.45, -hw * 0.68),
                                        w(hl * 0.12, -hw * 0.66)};
    mb.convexPolygon(roofPanel, roof);

    // Lights.
    const Rgba head{255, 248, 225, 255};
    const Rgba tailOff{120, 20, 24, 255};
    const Rgba tailOn{255, 40, 48, 255};
    const Rgba amber{255, 170, 0, 255};
    const Rgba reverseWhite{240, 240, 240, 255};
    const auto lamp = [&](double x0, double x1, double y0, double y1, Rgba c) {
        mb.quad(w(x0, y0), w(x1, y0), w(x1, y1), w(x0, y1), c);
    };
    lamp(hl - 0.12, hl - 0.02, hw * 0.45, hw * 0.85, head);
    lamp(hl - 0.12, hl - 0.02, -hw * 0.85, -hw * 0.45, head);
    const Rgba tail = look.lights.braking ? tailOn : tailOff;
    lamp(-hl + 0.02, -hl + 0.14, hw * 0.40, hw * 0.85, tail);
    lamp(-hl + 0.02, -hl + 0.14, -hw * 0.85, -hw * 0.40, tail);
    if (look.lights.reversing) {
        lamp(-hl + 0.02, -hl + 0.10, -hw * 0.20, hw * 0.20, reverseWhite);
    }
    if (look.lights.leftSignalOn) {
        lamp(hl - 0.30, hl - 0.05, hw - 0.12, hw + 0.02, amber);
        lamp(-hl + 0.05, -hl + 0.30, hw - 0.12, hw + 0.02, amber);
    }
    if (look.lights.rightSignalOn) {
        lamp(hl - 0.30, hl - 0.05, -hw - 0.02, -hw + 0.12, amber);
        lamp(-hl + 0.05, -hl + 0.30, -hw - 0.02, -hw + 0.12, amber);
    }
}

void buildTrackedObjectsMesh(std::span<const TrackedObject> objects, Vec2 origin, std::vector<ColoredVertex>& out) {
    MeshBuilder mb(out, origin);
    for (const TrackedObject& o : objects) {
        VehicleAppearance look;
        look.length = static_cast<double>(o.length);
        look.width = static_cast<double>(o.width);
        look.body = Rgba{112, 122, 136, 255};
        look.glass = Rgba{44, 50, 60, 255};
        appendVehicleMesh(mb, Pose2{{o.x, o.y}, static_cast<double>(o.heading)}, look);
    }
}

} // namespace rvn
