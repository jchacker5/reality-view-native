#include "road.hpp"

#include <QtTest>

#include <cmath>

using namespace rvn;

class TstRoad : public QObject {
    Q_OBJECT

private slots:
    void deterministicForSeed() {
        Road a(42);
        Road b(42);
        Road c(43);
        a.ensureLength(3000);
        b.ensureLength(3000);
        c.ensureLength(3000);
        const RoadPoint pa = a.at(2500.0);
        const RoadPoint pb = b.at(2500.0);
        const RoadPoint pc = c.at(2500.0);
        QCOMPARE(pa.position.x, pb.position.x);
        QCOMPARE(pa.position.y, pb.position.y);
        QVERIFY(length(pa.position - pc.position) > 1.0);
    }

    void startsStraightAndStaysSmooth() {
        Road r(7);
        r.ensureLength(5000);
        QVERIFY(std::abs(r.at(0.0).curvature) < 1e-12);
        const auto pts = r.points();
        for (std::size_t i = 1; i < pts.size(); ++i) {
            // Consecutive points are spacing apart and heading changes gently.
            QVERIFY(std::abs(length(pts[i].position - pts[i - 1].position) - 1.0) < 1e-3);
            QVERIFY(std::abs(pts[i].heading - pts[i - 1].heading) < 0.01);
            QVERIFY(std::abs(pts[i].curvature) < 0.0045);
        }
    }

    void extendsLazily() {
        Road r(1);
        const double before = r.length();
        r.ensureLength(before + 500.0);
        QVERIFY(r.length() >= before + 500.0);
    }

    void projectionRecoversLateralOffset() {
        Road r(11);
        r.ensureLength(2000);
        for (double s : {150.0, 640.0, 1333.0}) {
            for (double lat : {-4.0, 0.0, 2.5}) {
                const Vec2 p = r.pointAt(s, lat);
                const RoadProjection proj = r.project(p, s + 10.0);
                QVERIFY2(std::abs(proj.s - s) < 0.05, qPrintable(QString::number(proj.s)));
                QVERIFY2(std::abs(proj.lateral - lat) < 0.02, qPrintable(QString::number(proj.lateral)));
            }
        }
    }

    void hintedProjectionMatchesBruteForce() {
        Road r(5);
        r.ensureLength(4000);
        for (double s = 50.0; s < 3900.0; s += 97.0) {
            const Vec2 p = r.pointAt(s, 3.0);
            const RoadProjection fast = r.project(p, s - 20.0);
            const RoadProjection slow = r.projectBruteForce(p);
            QVERIFY(std::abs(fast.s - slow.s) < 1e-9);
            QVERIFY(std::abs(fast.lateral - slow.lateral) < 1e-9);
        }
    }

    void lanes() {
        Road r(1); // 3 lanes, 3.7 m
        QVERIFY(std::abs(r.laneCenterOffset(0) + 3.7) < 1e-12);
        QCOMPARE(r.laneCenterOffset(1), 0.0);
        QVERIFY(std::abs(r.laneCenterOffset(2) - 3.7) < 1e-12);
        QCOMPARE(r.laneAt(-3.7), 0);
        QCOMPARE(r.laneAt(0.0), 1);
        QCOMPARE(r.laneAt(3.0), 2);
        QCOMPARE(r.laneAt(-50.0), 0); // clamped
        QCOMPARE(r.laneAt(50.0), 2);
        QVERIFY(std::abs(r.halfWidth() - 5.55) < 1e-12);
    }
};

QTEST_GUILESS_MAIN(TstRoad)
#include "tst_road.moc"
