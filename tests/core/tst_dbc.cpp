#include "can_source.hpp"
#include "dbc.hpp"
#include "drive_simulator.hpp"

#include <QtTest>

#include <cmath>
#include <sstream>

using namespace rvn;

namespace {

constexpr const char* kVehicleDbc = R"DBC(
VERSION ""
BS_:
BU_: Chassis Steering ADAS Localization

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

} // namespace

class TstDbc : public QObject {
    Q_OBJECT

private slots:
    void intelAndMotorolaByteOrders() {
        std::stringstream ss(R"DBC(
BO_ 512 TestFrame: 8 ECU
 SG_ IntelTemp : 0|8@1+ (1,-40) [-40|215] "C" Vector__XXX
 SG_ IntelSigned : 8|12@1- (0.25,0) [-512|511.75] "deg" Vector__XXX
 SG_ MotoSigned : 31|16@0- (0.1,0) [-3276.8|3276.7] "deg" Vector__XXX
 SG_ MotoUnaligned : 43|12@0+ (0.5,10) [10|2057.5] "rpm" Vector__XXX
)DBC");
        const DbcDatabase db = DbcDatabase::parse(ss);
        const DbcMessage* msg = db.findMessage(512);
        QVERIFY(msg != nullptr);

        std::array<std::uint8_t, 8> payload{};
        const auto* temp = msg->findSignal("IntelTemp");
        const auto* isig = msg->findSignal("IntelSigned");
        const auto* msig = msg->findSignal("MotoSigned");
        const auto* munaligned = msg->findSignal("MotoUnaligned");
        QVERIFY(temp != nullptr && isig != nullptr && msig != nullptr && munaligned != nullptr);

        temp->encode(25.0, payload);        // raw = 65 = 0x41
        isig->encode(-12.5, payload);       // raw = -50 (bits 8..19: byte 1 + low nibble of byte 2)
        msig->encode(-123.4, payload);      // raw = -1234 = 0xFB2E (Motorola bytes 3 and 4)
        munaligned->encode(512.5, payload); // raw = 1005 (Motorola byte 5 low nibble + byte 6)

        QCOMPARE(payload[0], static_cast<std::uint8_t>(0x41));
        // Motorola 16-bit starting at bit 31 (byte 3 MSB) writes big-endian 0xFB, 0x2E into bytes 3 and 4.
        QCOMPARE(payload[3], static_cast<std::uint8_t>(0xFB));
        QCOMPARE(payload[4], static_cast<std::uint8_t>(0x2E));

        QVERIFY(std::abs(temp->decode(payload) - 25.0) < 1e-9);
        QVERIFY(std::abs(isig->decode(payload) - (-12.5)) < 1e-9);
        QVERIFY(std::abs(msig->decode(payload) - (-123.4)) < 1e-6);
        QVERIFY(std::abs(munaligned->decode(payload) - 512.5) < 1e-6);
    }

    void extendedCanIdsAndTelemetryRoundTrip() {
        std::stringstream ss(std::string(kVehicleDbc) + R"DBC(
BO_ 2147483904 ExtDiag: 4 Diag
 SG_ Alive : 0|8@1+ (1,0) [0|255] "" Vector__XXX
)DBC");
        const DbcDatabase db = DbcDatabase::parse(ss);
        QVERIFY(db.findMessage(0x100, false) != nullptr);
        QVERIFY(db.findMessage(0x100, true) != nullptr); // 0x80000100 = 2147483904

        DriveSimulator sim;
        sim.apply(cmd::SignalTurn{TurnSignal::Left});
        for (int i = 0; i < 60; ++i) {
            sim.step(0.02);
        }
        const TelemetrySample expected = sim.sample();

        const CanTelemetryCodec codec(db);
        const std::vector<CanFrame> frames = codec.encode(expected, sim.roadSeed());
        QCOMPARE(frames.size(), std::size_t{4});

        TelemetrySample decoded;
        std::uint64_t decodedSeed = 0;
        for (const CanFrame& f : frames) {
            QVERIFY(codec.decodeInto(f, decoded, decodedSeed));
        }

        QCOMPARE(decodedSeed, sim.roadSeed());
        QCOMPARE(decoded.gear, expected.gear);
        QCOMPARE(decoded.turnSignal, expected.turnSignal);
        QCOMPARE(decoded.autopilot, expected.autopilot);
        QCOMPARE(decoded.lane, expected.lane);
        QVERIFY(std::abs(decoded.speedMps - expected.speedMps) < 0.02F);
        QVERIFY(std::abs(decoded.steeringWheelDeg - expected.steeringWheelDeg) < 0.2F);
        QVERIFY(std::abs(decoded.x - expected.x) < 0.05);
        QVERIFY(std::abs(decoded.y - expected.y) < 0.05);
        QVERIFY(std::abs(decoded.heading - expected.heading) < 0.001F);
        QVERIFY(std::abs(decoded.latitude - expected.latitude) < 1e-5);
        QVERIFY(std::abs(decoded.longitude - expected.longitude) < 1e-5);
    }

    void rejectsMalformedDbc_data() {
        QTest::addColumn<QString>("dbc");
        QTest::addColumn<int>("line");
        QTest::newRow("sg before bo") << "SG_ Speed : 0|8@1+ (1,0) [0|10] \"\" Vector__XXX\n" << 1;
        QTest::newRow("duplicate id") << "BO_ 100 A: 8 ECU\nBO_ 100 B: 8 ECU\n" << 2;
        QTest::newRow("dlc out of range") << "BO_ 100 A: 9 ECU\n" << 1;
        QTest::newRow("intel bit overflow") << "BO_ 100 A: 4 ECU\n SG_ S : 28|8@1+ (1,0) [0|1] \"\" Vector__XXX\n" << 2;
        QTest::newRow("motorola bit overflow") << "BO_ 100 A: 2 ECU\n SG_ S : 12|8@0+ (1,0) [0|1] \"\" Vector__XXX\n"
                                               << 2;
        QTest::newRow("bad scale") << "BO_ 100 A: 8 ECU\n SG_ S : 0|8@1+ (abc,0) [0|1] \"\" Vector__XXX\n" << 2;
    }

    void rejectsMalformedDbc() {
        QFETCH(QString, dbc);
        QFETCH(int, line);
        std::stringstream ss(dbc.toStdString());
        try {
            (void)DbcDatabase::parse(ss);
            QFAIL("expected DbcParseError");
        } catch (const DbcParseError& e) {
            QCOMPARE(e.line(), static_cast<std::size_t>(line));
        }
    }
};

QTEST_GUILESS_MAIN(TstDbc)
#include "tst_dbc.moc"
