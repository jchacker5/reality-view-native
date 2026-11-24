#include "vehicle_model.hpp"

#include <QtTest>

#include <cmath>

using namespace rvn;

namespace {
constexpr double kDt = 0.005;
void run(VehicleModel& m, const ActuatorInput& in, double seconds) {
    const int steps = static_cast<int>(seconds / kDt);
    for (int i = 0; i < steps; ++i) {
        m.step(in, kDt);
    }
}
} // namespace

class TstVehicleModel : public QObject {
    Q_OBJECT

private slots:
    void acceleratesStraight() {
        VehicleModel m;
        run(m, ActuatorInput{1.0, 0.0, 0.0, Gear::Drive}, 2.0);
        QVERIFY(m.state().speed > 7.0);
        QVERIFY(std::abs(m.state().pose.position.y) < 1e-9);
        QVERIFY(m.state().pose.position.x > 0.0);
    }

    void brakingStopsAndNeverReverses() {
        VehicleModel m;
        m.reset({}, 20.0);
        run(m, ActuatorInput{0.0, 1.0, 0.0, Gear::Drive}, 5.0);
        QCOMPARE(m.state().speed, 0.0);
        // Holding the brake must not creep backwards.
        run(m, ActuatorInput{0.0, 1.0, 0.0, Gear::Drive}, 1.0);
        QCOMPARE(m.state().speed, 0.0);
    }

    void coastsDownWithoutBrake() {
        VehicleModel m;
        m.reset({}, 30.0);
        run(m, ActuatorInput{0.0, 0.0, 0.0, Gear::Neutral}, 3.0);
        QVERIFY(m.state().speed < 30.0 && m.state().speed > 25.0);
    }

    void reverseGearGoesBackwards() {
        VehicleModel m;
        run(m, ActuatorInput{1.0, 0.0, 0.0, Gear::Reverse}, 1.0);
        QVERIFY(m.state().speed < 0.0);
        QVERIFY(m.state().speed >= -m.params().maxReverseSpeed);
        QVERIFY(m.state().pose.position.x < 0.0);
    }

    void parkHoldsStill() {
        VehicleModel m;
        run(m, ActuatorInput{1.0, 0.0, 0.0, Gear::Park}, 1.0);
        QCOMPARE(m.state().speed, 0.0);
    }

    void steeringIsRateLimitedAndClamped() {
        VehicleModel m;
        m.step(ActuatorInput{0.0, 0.0, 10.0, Gear::Drive}, 0.1);
        QVERIFY(std::abs(m.state().roadWheelAngle - m.params().maxSteerRate * 0.1) < 1e-12);
        run(m, ActuatorInput{0.0, 0.0, 10.0, Gear::Drive}, 2.0);
        QCOMPARE(m.state().roadWheelAngle, m.params().maxRoadWheelAngle);
    }

    // Bicycle model: steady-state turning radius R = L / tan(delta).
    void turningRadiusMatchesBicycleGeometry() {
        VehicleModel m;
        const double delta = 0.1;
        m.reset({}, 10.0);
        // Pre-set the wheel angle, then hold speed with just enough throttle to cancel drag.
        for (int i = 0; i < 400; ++i) {
            m.step(ActuatorInput{0.0, 0.0, delta, Gear::Neutral}, kDt);
        }
        const double expectedYawRate = m.state().speed * std::tan(delta) / m.params().wheelbaseM;
        QVERIFY(std::abs(m.state().yawRate - expectedYawRate) < 1e-9);
        const double radius = m.params().wheelbaseM / std::tan(delta);
        QVERIFY(std::abs(radius - 28.6) < 0.1);
    }

    void ignoresNonPositiveDt() {
        VehicleModel m;
        m.reset({{1.0, 2.0}, 0.3}, 5.0);
        m.step(ActuatorInput{1.0, 0.0, 0.0, Gear::Drive}, 0.0);
        m.step(ActuatorInput{1.0, 0.0, 0.0, Gear::Drive}, -1.0);
        QCOMPARE(m.state().pose.position.x, 1.0);
        QCOMPARE(m.state().speed, 5.0);
    }
};

QTEST_GUILESS_MAIN(TstVehicleModel)
#include "tst_vehicle_model.moc"
