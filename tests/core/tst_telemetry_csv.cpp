#include "drive_simulator.hpp"
#include "replay_source.hpp"
#include "telemetry_csv.hpp"

#include <QtTest>

#include <sstream>

using namespace rvn;

namespace {

TelemetryLog recordDrive(double seconds, double hz) {
    DriveSimulator sim;
    TelemetryLog log;
    log.roadSeed = sim.roadSeed();
    const int steps = static_cast<int>(seconds * hz);
    for (int i = 0; i < steps; ++i) {
        sim.step(1.0 / hz);
        log.samples.push_back(sim.sample());
    }
    return log;
}

std::string header() {
    return "# reality-view-native telemetry v1 road_seed=9\n"
           "t,x,y,heading,speed,accel,yaw_rate,steering_deg,throttle,brake,set_speed,lane_offset,road_s,lane,"
           "gear,turn_signal,autopilot,lat,lon\n";
}

} // namespace

class TstTelemetryCsv : public QObject {
    Q_OBJECT

private slots:
    void roundTripIsExact() {
        const TelemetryLog original = recordDrive(2.0, 50.0);
        std::stringstream ss;
        writeTelemetryCsv(ss, original);
        const TelemetryLog parsed = readTelemetryCsv(ss);
        QCOMPARE(parsed.roadSeed, original.roadSeed);
        QCOMPARE(parsed.samples.size(), original.samples.size());
        for (std::size_t i = 0; i < parsed.samples.size(); ++i) {
            const auto& a = original.samples[i];
            const auto& b = parsed.samples[i];
            QCOMPARE(b.x, a.x); // shortest round-trip formatting: bit-exact
            QCOMPARE(b.heading, a.heading);
            QCOMPARE(b.speedMps, a.speedMps);
            QCOMPARE(b.gear, a.gear);
            QCOMPARE(b.autopilot, a.autopilot);
            QCOMPARE(b.latitude, a.latitude);
        }
    }

    void rejectsMalformedInput_data() {
        QTest::addColumn<QString>("body");
        QTest::addColumn<int>("line");
        QTest::newRow("bad number") << "0,abc,0,0,0,0,0,0,0,0,0,0,0,1,D,0,2,37,-121\n" << 3;
        QTest::newRow("bad gear") << "0,0,0,0,0,0,0,0,0,0,0,0,0,1,X,0,2,37,-121\n" << 3;
        QTest::newRow("short row") << "0,0,0\n" << 3;
        QTest::newRow("long row") << "0,0,0,0,0,0,0,0,0,0,0,0,0,1,D,0,2,37,-121,5\n" << 3;
        QTest::newRow("enum range") << "0,0,0,0,0,0,0,0,0,0,0,0,0,1,D,7,2,37,-121\n" << 3;
        QTest::newRow("time backwards") << "1,0,0,0,0,0,0,0,0,0,0,0,0,1,D,0,2,37,-121\n"
                                           "0.5,0,0,0,0,0,0,0,0,0,0,0,0,1,D,0,2,37,-121\n"
                                        << 4;
    }
    void rejectsMalformedInput() {
        QFETCH(QString, body);
        QFETCH(int, line);
        std::stringstream ss(header() + body.toStdString());
        try {
            (void)readTelemetryCsv(ss);
            QFAIL("expected CsvParseError");
        } catch (const CsvParseError& e) {
            QCOMPARE(e.line(), static_cast<std::size_t>(line));
        }
    }

    void rejectsMissingHeader() {
        std::stringstream ss("t,x,y\n");
        QVERIFY_THROWS_EXCEPTION(CsvParseError, (void)readTelemetryCsv(ss));
    }

    void replayInterpolatesAndLoops() {
        TelemetryLog log;
        TelemetrySample a;
        a.simTimeS = 0.0;
        a.x = 0.0;
        a.speedMps = 10.0F;
        TelemetrySample b = a;
        b.simTimeS = 1.0;
        b.x = 10.0;
        b.speedMps = 20.0F;
        log.samples = {a, b};
        ReplaySource replay(log);
        replay.step(0.25);
        QVERIFY(std::abs(replay.sample().x - 2.5) < 1e-9);
        QVERIFY(std::abs(static_cast<double>(replay.sample().speedMps) - 12.5) < 1e-4);
        replay.step(1.0); // wraps to 0.25 again
        QVERIFY(std::abs(replay.playhead() - 0.25) < 1e-9);
        replay.apply(cmd::Reset{});
        QCOMPARE(replay.playhead(), 0.0);
    }

    void replayRejectsTinyLog() {
        TelemetryLog log;
        log.samples.resize(1);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, ReplaySource{log});
    }
};

QTEST_GUILESS_MAIN(TstTelemetryCsv)
#include "tst_telemetry_csv.moc"
