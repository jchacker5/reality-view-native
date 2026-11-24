// Headless recorder: runs the simulator with a scripted drive and writes a telemetry CSV that
// the app can replay (`reality-view-native --replay drive.csv`).
//
//   rvn_record [--seconds 90] [--hz 50] [--seed 7] [--out drive.csv]
#include "drive_simulator.hpp"
#include "telemetry_csv.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    double seconds = 90.0;
    double hz = 50.0;
    std::uint64_t seed = 7;
    std::string out = "drive.csv";
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view flag = argv[i];
        const char* value = argv[i + 1];
        if (flag == "--seconds")
            seconds = std::atof(value);
        else if (flag == "--hz")
            hz = std::atof(value);
        else if (flag == "--seed")
            seed = std::strtoull(value, nullptr, 10);
        else if (flag == "--out")
            out = value;
        else {
            std::fprintf(stderr, "unknown flag %s\n", argv[i]);
            return 2;
        }
    }
    if (seconds <= 0.0 || hz <= 0.0) {
        std::fprintf(stderr, "--seconds and --hz must be positive\n");
        return 2;
    }

    rvn::SimConfig cfg;
    cfg.seed = seed;
    rvn::DriveSimulator sim(cfg);
    rvn::TelemetryLog log;
    log.roadSeed = seed;

    // Script: lane change left at 15 s, back right at 45 s, slow to 55 mph at 60 s.
    const auto steps = static_cast<long>(seconds * hz);
    const double dt = 1.0 / hz;
    for (long i = 0; i < steps; ++i) {
        if (i == static_cast<long>(15.0 * hz))
            sim.apply(rvn::cmd::SignalTurn{rvn::TurnSignal::Left});
        if (i == static_cast<long>(45.0 * hz))
            sim.apply(rvn::cmd::SignalTurn{rvn::TurnSignal::Right});
        if (i == static_cast<long>(60.0 * hz))
            sim.apply(rvn::cmd::SetCruiseSpeed{24.6F});
        sim.step(dt);
        rvn::TelemetrySample s = sim.sample();
        s.seq = static_cast<std::uint64_t>(i);
        log.samples.push_back(s);
    }

    std::ofstream file(out);
    if (!file) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    rvn::writeTelemetryCsv(file, log);
    std::printf("wrote %zu samples (%.0f s at %.0f Hz) to %s\n", log.samples.size(), seconds, hz, out.c_str());
    return 0;
}
