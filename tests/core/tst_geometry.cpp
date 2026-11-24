#include "geometry.hpp"

#include <QtTest>

#include <numbers>

using namespace rvn;

class TstGeometry : public QObject {
    Q_OBJECT

private slots:
    void wrapAngleRange_data() {
        QTest::addColumn<double>("in");
        QTest::addColumn<double>("expected");
        QTest::newRow("zero") << 0.0 << 0.0;
        QTest::newRow("3pi") << 3.0 * std::numbers::pi << -std::numbers::pi;
        QTest::newRow("-3pi/2") << -1.5 * std::numbers::pi << 0.5 * std::numbers::pi;
        QTest::newRow("big") << 100.0 << std::remainder(100.0, 2.0 * std::numbers::pi);
    }
    void wrapAngleRange() {
        QFETCH(double, in);
        QFETCH(double, expected);
        const double w = wrapAngle(in);
        QVERIFY(w >= -std::numbers::pi && w < std::numbers::pi);
        QVERIFY(std::abs(w - expected) < 1e-9);
    }

    void poseRoundTrip() {
        const Pose2 pose{{1234.5, -987.25}, 0.7};
        const Vec2 world{1240.0, -980.0};
        const Vec2 back = pose.toWorld(pose.toLocal(world));
        QVERIFY(std::abs(back.x - world.x) < 1e-9);
        QVERIFY(std::abs(back.y - world.y) < 1e-9);
    }

    void localFrameIsForwardLeft() {
        const Pose2 pose{{10.0, 10.0}, std::numbers::pi / 2}; // facing north
        const Vec2 ahead = pose.toLocal({10.0, 15.0});
        QVERIFY(std::abs(ahead.x - 5.0) < 1e-9 && std::abs(ahead.y) < 1e-9);
        const Vec2 left = pose.toLocal({5.0, 10.0}); // west is to the left when facing north
        QVERIFY(std::abs(left.x) < 1e-9 && std::abs(left.y - 5.0) < 1e-9);
    }

    void latLonOffsets() {
        const GeoOrigin o{};
        const LatLon north = enuToLatLon(o, {0.0, 1000.0});
        // 1 km north is ~0.008983 degrees of latitude.
        QVERIFY(std::abs((north.latitudeDeg - o.latitudeDeg) - 0.0089832) < 1e-6);
        QCOMPARE(north.longitudeDeg, o.longitudeDeg);
        const LatLon east = enuToLatLon(o, {1000.0, 0.0});
        QVERIFY(east.longitudeDeg > o.longitudeDeg);
    }
};

QTEST_GUILESS_MAIN(TstGeometry)
#include "tst_geometry.moc"
