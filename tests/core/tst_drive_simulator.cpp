#include "drive_simulator.hpp"

#include <QtTest>

#include <cmath>

using namespace rvn;

namespace {
constexpr double kDt = 1.0 / 240.0;
void run(DriveSimulator& sim, double seconds) {
    const int steps = static_cast<int>(seconds / kDt);
    for (int i = 0; i < steps; ++i) {
        sim.step(kDt);
    }
}
} // namespace

class TstDriveSimulator : public QObject {
    Q_OBJECT

private slots:
    void startsEngagedAndMoving() {
        DriveSimulator sim;
        QCOMPARE(sim.autopilot(), AutopilotState::Active);
        QCOMPARE(sim.gear(), Gear::Drive);
        QVERIFY(sim.ego().speed > 20.0);
    }

    void autopilotHoldsLaneForTwoMinutes() {
        DriveSimulator sim;
        double worstOffset = 0.0;
        for (int i = 0; i < 120 * 240; ++i) {
            sim.step(kDt);
            const double offset = sim.egoOnRoad().lateral - sim.road().laneCenterOffset(sim.targetLane());
            worstOffset = std::max(worstOffset, std::abs(offset));
        }
        QCOMPARE(sim.autopilot(), AutopilotState::Active);
        QVERIFY2(worstOffset < 0.5, qPrintable(QString("worst lane offset %1 m").arg(worstOffset)));
        QVERIFY(sim.egoOnRoad().s > 2000.0);
    }

    void adaptiveCruiseNeverCloserThanMinGap() {
        SimConfig cfg;
        cfg.trafficCount = 8;
        DriveSimulator sim(cfg);
        double closest = 1e9;
        for (int i = 0; i < 180 * 240; ++i) {
            sim.step(kDt);
            if (const auto gap = sim.egoLeadGap()) {
                closest = std::min(closest, *gap);
            }
        }
        QVERIFY2(closest > 2.0, qPrintable(QString("closest gap %1 m").arg(closest)));
    }

    void cruiseSpeedIsRespected() {
        SimConfig cfg;
        cfg.trafficCount = 0;
        DriveSimulator sim(cfg);
        sim.apply(cmd::SetCruiseSpeed{20.0F});
        run(sim, 40.0);
        QVERIFY2(std::abs(sim.ego().speed - 20.0) < 0.6, qPrintable(QString::number(sim.ego().speed)));
    }

    void turnSignalChangesLaneThenCancels() {
        SimConfig cfg;
        cfg.trafficCount = 0;
        DriveSimulator sim(cfg);
        run(sim, 2.0);
        const int startLane = sim.targetLane();
        sim.apply(cmd::SignalTurn{TurnSignal::Left});
        run(sim, 8.0);
        QCOMPARE(sim.targetLane(), startLane + 1);
        QCOMPARE(sim.road().laneAt(sim.egoOnRoad().lateral), startLane + 1);
        QCOMPARE(sim.turnSignal(), TurnSignal::Off);
    }

    void driverBrakeDisengages() {
        DriveSimulator sim;
        sim.apply(cmd::ManualInput{0.0F, 0.6F, 0.0F});
        QCOMPARE(sim.autopilot(), AutopilotState::Standby);
        run(sim, 6.0);
        QCOMPARE(sim.ego().speed, 0.0);
    }

    void shiftToParkOnlyWhenStopped() {
        DriveSimulator sim;
        sim.apply(cmd::ShiftGear{Gear::Park});
        QCOMPARE(sim.gear(), Gear::Drive); // rejected at speed
        sim.apply(cmd::ManualInput{0.0F, 1.0F, 0.0F});
        run(sim, 6.0);
        sim.apply(cmd::ShiftGear{Gear::Park});
        QCOMPARE(sim.gear(), Gear::Park);
        QCOMPARE(sim.autopilot(), AutopilotState::Unavailable);
    }

    void deterministicForSeed() {
        DriveSimulator a;
        DriveSimulator b;
        run(a, 30.0);
        run(b, 30.0);
        const TelemetrySample sa = a.sample();
        const TelemetrySample sb = b.sample();
        QCOMPARE(sa.x, sb.x);
        QCOMPARE(sa.y, sb.y);
        QCOMPARE(sa.objectCount, sb.objectCount);
    }

    void sampleReportsNearestObjectsSorted() {
        DriveSimulator sim;
        run(sim, 5.0);
        const TelemetrySample s = sim.sample();
        QVERIFY(s.objectCount > 0);
        QVERIFY(s.objectCount <= kMaxTrackedObjects);
        double prev = 0.0;
        for (std::size_t i = 0; i < s.objectCount; ++i) {
            const double d = std::hypot(s.objects[i].x - s.x, s.objects[i].y - s.y);
            QVERIFY(d + 5.0 >= prev); // sorted by along-road distance, ~monotone in Euclidean distance
            prev = d;
        }
        QVERIFY(s.latitude != 0.0 && s.longitude != 0.0);
    }

    void resetRestoresInitialState() {
        DriveSimulator sim;
        const TelemetrySample before = sim.sample();
        run(sim, 10.0);
        sim.apply(cmd::Reset{});
        const TelemetrySample after = sim.sample();
        QCOMPARE(after.x, before.x);
        QCOMPARE(after.simTimeS, 0.0);
    }
};

QTEST_GUILESS_MAIN(TstDriveSimulator)
#include "tst_drive_simulator.moc"
