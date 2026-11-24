#pragma once

#include "geometry.hpp"
#include "road.hpp"
#include "telemetry.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace rvn {

/// Byte-compatible with QSGGeometry::ColoredPoint2D (float x, y; uchar r, g, b, a), so the
/// renderer can memcpy a whole mesh into a scene-graph geometry. Colours are premultiplied
/// by alpha, which is what Qt's vertex-colour material expects.
struct ColoredVertex {
    float x;
    float y;
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};
static_assert(sizeof(ColoredVertex) == 12);

struct Rgba {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};

    /// Straight-alpha colour -> premultiplied. `alpha` is 0..1.
    [[nodiscard]] static constexpr Rgba premultiplied(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                                      double alpha) noexcept {
        const double a = alpha < 0.0 ? 0.0 : (alpha > 1.0 ? 1.0 : alpha);
        return Rgba{static_cast<std::uint8_t>(r * a + 0.5), static_cast<std::uint8_t>(g * a + 0.5),
                    static_cast<std::uint8_t>(b * a + 0.5), static_cast<std::uint8_t>(255.0 * a + 0.5)};
    }
};

/// Appends triangles (DrawTriangles topology, no index buffer) relative to a floating origin.
/// Positions are computed in double and narrowed to float only after subtracting the origin.
class MeshBuilder {
public:
    MeshBuilder(std::vector<ColoredVertex>& out, Vec2 origin) noexcept : out_(out), origin_(origin) {}

    void triangle(Vec2 a, Vec2 b, Vec2 c, Rgba color);
    void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Rgba color); ///< a-b-c-d in order around the quad
    /// Convex polygon as a triangle fan.
    void convexPolygon(std::span<const Vec2> points, Rgba color);

private:
    void vertex(Vec2 p, Rgba c);
    std::vector<ColoredVertex>& out_;
    Vec2 origin_;
};

struct VehicleLights {
    bool braking{};
    bool leftSignalOn{};
    bool rightSignalOn{};
    bool reversing{};
};

struct VehicleAppearance {
    double length{4.72};
    double width{1.85};
    double roadWheelAngle{};
    Rgba body{232, 236, 240, 255};
    Rgba glass{40, 48, 58, 255};
    VehicleLights lights{};
};

/// Road surface, shoulders, solid edge lines and dashed lane dividers between sFrom and sTo.
/// Roughly 35 vertices per metre of road.
void buildRoadMesh(const Road& road, double sFrom, double sTo, Vec2 origin, std::vector<ColoredVertex>& out);

/// The ribbon the driver-assist stack plans to follow (the blue path ahead of the car).
void buildPlannedPathMesh(const Road& road, double sFrom, double lengthM, double lateral, Vec2 origin, Rgba color,
                          std::vector<ColoredVertex>& out);

/// Fading trail behind the vehicle, oldest point first.
void buildTrailMesh(std::span<const Vec2> points, double widthM, Vec2 origin, std::vector<ColoredVertex>& out);

/// A stylized top-down car at `pose` (relative to `origin`): body, glass, wheels (fronts
/// steered), and lights.
void appendVehicleMesh(MeshBuilder& mb, const Pose2& pose, const VehicleAppearance& look);

/// Every tracked object as a stylized car.
void buildTrackedObjectsMesh(std::span<const TrackedObject> objects, Vec2 origin, std::vector<ColoredVertex>& out);

} // namespace rvn
