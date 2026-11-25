#include "can_bus.hpp"
#include "can_source.hpp"
#include "dbc.hpp"
#include "drive_simulator.hpp"
#include "telemetry_consumer.hpp"
#include "telemetry_worker.hpp"

#include <QtTest>

#include <cmath>
#include <sstream>
#include <thread>

using namespace rvn;

namespace {

constexpr const char* kTestDbc = R"DBC(
BO_ 256 ID256_ChassisMotion: 8 Chassis
 SG_ VehicleSpeed : 0|16@1+ (0.01,0) [0|100] "m/s" Vector__XXX
 SG_ LongitudinalAccel : 16|12@1- (0.01,0) [-20.48|20.47] "m/s^2" Vector__XXX
 SG_ YawRate : 28|12@1- (0.002,0) [-4.096|4.095] "rad/s" Vector__XXX
 SG_ GearState : 40|3@1+ (1,0) [0|7] "" Vector__XXX
 SG_ RoadSeed : 48|16@1+ (1,0) [0|65535] "" Vector__XXX

BO_ 257 ID257_DriverInputs: 8 Steering
 SG_ SteeringWheelAngle : 7|16@0- (0.1,0) [-720|720] "deg" Vector__XXX
 SG_ ThrottlePos : 16|8@1+ (0.005,0) [0|1] "ratio" Vector__XXX
 SG_ BrakePos : 24|8@1+ (0.005,0) [0|1] "ratio" Vector__XXX
 SG_ TurnSignalState : 32|2@1+ (1,0) [0|3] "" Vector__XXX

BO_ 258 ID258_AutopilotStatus: 8 ADAS
 SG_ AutopilotState : 0|2@1+ (1,0) [0|3] "" Vector__XXX
 SG_ CruiseSetSpeed : 8|12@1+ (0.02,0) [0|81.9] "m/s" Vector__XXX
 SG_ LaneIndex : 20|4@1+ (1,0) [0|15] "" Vector__XXX
 SG_ LaneOffset : 24|12@1- (0.005,0) [-10.24|10.235] "m" Vector__XXX
 SG_ RoadArcLength : 36|24@1+ (0.05,0) [0|838860] "m" Vector__XXX

BO_ 259 ID259_VehiclePose: 8 Localization
 SG_ WorldPosX : 0|24@1- (0.02,0) [-167772|167772] "m" Vector__XXX
 SG_ WorldPosY : 24|24@1- (0.02,0) [-167772|167772] "m" Vector__XXX
 SG_ VehicleHeading : 48|16@1- (0.0001,0) [-3.2768|3.2767] "rad" Vector__XXX
)DBC";

DbcDatabase parseTestDbc() {
    std::stringstream ss(kTestDbc);
    return DbcDatabase::parse(ss);
}

} // namespace

class TstCanSource : public QObject {
    Q_OBJECT

private slots:
    void candumpLogReplaysAndLoops() {
        const DbcDatabase db = parseTestDbc();
        const CanTelemetryCodec codec(db);

        DriveSimulator sim;
        std::stringstream candump;
        candump << "# test candump\n";
        double finalSpeed = 0.0;
        for (int i = 0; i <= 50; ++i) {
            if (i == 10) {
                sim.apply(cmd::SignalTurn{TurnSignal::Left});
            }
            sim.step(0.02);
            const TelemetrySample s = sim.sample();
            finalSpeed = static_cast<double>(s.speedMps);
            for (const CanFrame& f : codec.encode(s, sim.roadSeed())) {
                candump << formatCandumpLine(1000.0 + static_cast<double>(i) * 0.02, "vcan0", f) << '\n';
            }
        }

        const std::vector<CandumpEntry> entries = readCandumpLog(candump);
        QCOMPARE(entries.size(), std::size_t{51 * 4});
        QCOMPARE(entries.front().timestampS, 0.0);

        CanTelemetrySource source(db, entries);
        for (int i = 0; i < 50; ++i) {
            source.step(0.02);
        }
        const TelemetrySample decoded = source.sample();
        QCOMPARE(decoded.gear, Gear::Drive);
        QCOMPARE(decoded.autopilot, AutopilotState::Active);
        QVERIFY(std::abs(static_cast<double>(decoded.speedMps) - finalSpeed) < 0.05);

        // Looping past the 1.0 s log wraps cleanly back near t = 0 s.
        source.step(0.05);
        QCOMPARE(source.roadSeed(), sim.roadSeed());

        // Reset rewinds to the start.
        source.apply(cmd::Reset{});
        QCOMPARE(source.sample().simTimeS, 0.0);
    }

    void rejectsMalformedCandump_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("line");
        QTest::newRow("missing parens") << "0.0 vcan0 100#1122\n" << 1;
        QTest::newRow("bad timestamp") << "(abc) vcan0 100#1122\n" << 1;
        QTest::newRow("missing hash") << "(0.0) vcan0 1001122\n" << 1;
        QTest::newRow("odd payload length") << "(0.0) vcan0 100#123\n" << 1;
        QTest::newRow("bad hex byte") << "(0.0) vcan0 100#ZZ\n" << 1;
        QTest::newRow("non monotonic") << "(1.0) vcan0 100#11\n(0.5) vcan0 100#11\n" << 2;
    }

    void rejectsMalformedCandump() {
        QFETCH(QString, text);
        QFETCH(int, line);
        std::stringstream ss(text.toStdString());
        try {
            (void)readCandumpLog(ss);
            QFAIL("expected CandumpParseError");
        } catch (const CandumpParseError& e) {
            QCOMPARE(e.line(), static_cast<std::size_t>(line));
        }
    }

    void liveMemoryBusFlowsThroughTelemetryWorker() {
        const DbcDatabase db = parseTestDbc();
        const CanTelemetryCodec codec(db);

        auto bus = std::make_unique<MemoryCanBus>();
        MemoryCanBus* rawBus = bus.get();

        // Feed frames into the bus before and during worker execution.
        TelemetrySample target;
        target.speedMps = 31.5F;
        target.steeringWheelDeg = -18.2F;
        target.gear = Gear::Drive;
        target.autopilot = AutopilotState::Active;
        target.turnSignal = TurnSignal::Right;
        target.x = 240.0;
        target.y = -45.0;
        target.heading = 0.25F;
        for (const CanFrame& f : codec.encode(target, 11)) {
            QVERIFY(rawBus->send(f));
        }

        auto source = std::make_unique<CanTelemetrySource>(db, std::move(bus), 7);
        TelemetryWorker worker(std::move(source), 200);
        TelemetryConsumer consumer;
        worker.start();

        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                (void)consumer.drain(worker.samples());
                return consumer.latest().has_value() && consumer.latest()->speedMps > 30.0F;
            }(),
            3000);

        const TelemetrySample got = *consumer.latest();
        QCOMPARE(got.gear, Gear::Drive);
        QCOMPARE(got.turnSignal, TurnSignal::Right);
        QCOMPARE(got.autopilot, AutopilotState::Active);
        QVERIFY(std::abs(got.speedMps - 31.5F) < 0.05F);
        QVERIFY(std::abs(got.steeringWheelDeg - (-18.2F)) < 0.2F);
        QVERIFY(std::abs(got.x - 240.0) < 0.05);
        QVERIFY(std::abs(got.y - (-45.0)) < 0.05);

        worker.stop();
        QCOMPARE(consumer.totalGaps(), std::uint64_t{0});
    }
};

QTEST_GUILESS_MAIN(TstCanSource)
#include "tst_can_source.moc"
