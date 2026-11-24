#include "scene_geometry.hpp"

#include <QtTest>

#include <cmath>

using namespace rvn;

namespace {
bool allFinite(const std::vector<ColoredVertex>& v) {
    return std::all_of(v.begin(), v.end(),
                       [](const ColoredVertex& c) { return std::isfinite(c.x) && std::isfinite(c.y); });
}
} // namespace

class TstSceneGeometry : public QObject {
    Q_OBJECT

private slots:
    void premultipliedAlpha() {
        constexpr Rgba c = Rgba::premultiplied(200, 100, 50, 0.5);
        QCOMPARE(int(c.r), 100);
        QCOMPARE(int(c.g), 50);
        QCOMPARE(int(c.b), 25);
        QCOMPARE(int(c.a), 128);
        constexpr Rgba clamped = Rgba::premultiplied(10, 10, 10, 2.0);
        QCOMPARE(int(clamped.a), 255);
    }

    void roadMeshIsTrianglesAndFinite() {
        Road road(3);
        road.ensureLength(2000);
        std::vector<ColoredVertex> v;
        buildRoadMesh(road, 100.0, 700.0, road.at(400.0).position, v);
        QVERIFY(!v.empty());
        QCOMPARE(v.size() % 3, std::size_t{0});
        QVERIFY(allFinite(v));
        // Budget: ~35 vertices per metre. Guard against an accidental quadratic blow-up.
        QVERIFY2(v.size() < 600 * 45, qPrintable(QString::number(v.size())));
    }

    void floatingOriginKeepsCoordinatesSmall() {
        Road road(3);
        road.ensureLength(60'000);
        const Vec2 origin = road.at(55'000.0).position;
        std::vector<ColoredVertex> v;
        buildRoadMesh(road, 54'800.0, 55'200.0, origin, v);
        for (const auto& c : v) {
            QVERIFY(std::abs(c.x) < 300.0F && std::abs(c.y) < 300.0F);
        }
    }

    void emptyAndDegenerateInputs() {
        Road road(3);
        std::vector<ColoredVertex> v;
        buildRoadMesh(road, 500.0, 400.0, {}, v); // reversed range
        QVERIFY(v.empty());
        const std::vector<Vec2> one{{0, 0}};
        buildTrailMesh(one, 1.0, {}, v);
        QVERIFY(v.empty());
        const std::vector<Vec2> dup{{0, 0}, {0, 0}, {0, 0}};
        buildTrailMesh(dup, 1.0, {}, v); // zero-length segments must not produce NaN
        QVERIFY(allFinite(v));
    }

    void trailFadesFromOldToNew() {
        std::vector<Vec2> pts;
        for (int i = 0; i < 50; ++i) {
            pts.push_back({static_cast<double>(i), 0.0});
        }
        std::vector<ColoredVertex> v;
        buildTrailMesh(pts, 1.0, {}, v);
        QCOMPARE(v.size(), std::size_t{49 * 6});
        QVERIFY(v.front().a < v.back().a);
    }

    void vehicleMeshTracksPose() {
        std::vector<ColoredVertex> v;
        MeshBuilder mb(v, {100.0, 100.0});
        appendVehicleMesh(mb, Pose2{{110.0, 100.0}, 0.0}, VehicleAppearance{});
        QVERIFY(!v.empty());
        float minX = 1e9F;
        float maxX = -1e9F;
        for (const auto& c : v) {
            minX = std::min(minX, c.x);
            maxX = std::max(maxX, c.x);
        }
        // Centred at x = 10 (relative to the origin), ~4.7 m long.
        QVERIFY(std::abs((minX + maxX) / 2.0F - 10.0F) < 0.2F);
        QVERIFY(std::abs((maxX - minX) - 4.72F) < 0.2F);
    }

    void lightsAddGeometry() {
        std::vector<ColoredVertex> plain;
        std::vector<ColoredVertex> lit;
        MeshBuilder a(plain, {});
        MeshBuilder b(lit, {});
        VehicleAppearance look;
        appendVehicleMesh(a, {}, look);
        look.lights.leftSignalOn = true;
        look.lights.reversing = true;
        appendVehicleMesh(b, {}, look);
        QVERIFY(lit.size() > plain.size());
    }
};

QTEST_GUILESS_MAIN(TstSceneGeometry)
#include "tst_scene_geometry.moc"
