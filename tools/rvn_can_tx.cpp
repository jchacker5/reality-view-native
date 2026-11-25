// Headless CAN bus transmitter / candump generator: runs DriveSimulator, encodes each step
// through a DBC database, and either writes a standard candump log or streams live CAN frames
// onto a Linux SocketCAN interface (e.g. vcan0).
//
//   rvn_can_tx --dbc samples/vehicle.dbc --out samples/sample-drive.candump [--seconds 30] [--hz 50]
//   rvn_can_tx --dbc samples/vehicle.dbc --can vcan0 [--seconds 60] [--hz 100]
#include "can_bus.hpp"
#include "can_source.hpp"
#include "dbc.hpp"
#include "drive_simulator.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    std::string dbcPath;
    std::string outPath;
    std::string canIface;
    std::string logIface = "vcan0";
    double seconds = 30.0;
    double hz = 50.0;
    std::uint64_t seed = 7;

    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view flag = argv[i];
        const char* value = argv[i + 1];
        if (flag == "--dbc") {
            dbcPath = value;
        } else if (flag == "--out") {
            outPath = value;
        } else if (flag == "--can") {
            canIface = value;
        } else if (flag == "--iface") {
            logIface = value;
        } else if (flag == "--seconds") {
            seconds = std::atof(value);
        } else if (flag == "--hz") {
            hz = std::atof(value);
        } else if (flag == "--seed") {
            seed = std::strtoull(value, nullptr, 10);
        } else {
            std::fprintf(stderr, "unknown flag %s\n", argv[i]);
            return 2;
        }
    }

    if (dbcPath.empty() || (outPath.empty() && canIface.empty()) || (!outPath.empty() && !canIface.empty())) {
        std::fprintf(stderr, "usage: rvn_can_tx --dbc <file.dbc> (--out <file.candump> | --can <iface>) "
                             "[--seconds 30] [--hz 50] [--seed 7] [--iface vcan0]\n");
        return 2;
    }
    if (seconds <= 0.0 || hz <= 0.0) {
        std::fprintf(stderr, "--seconds and --hz must be positive\n");
        return 2;
    }

    try {
        std::ifstream dbcIn(dbcPath);
        if (!dbcIn) {
            std::fprintf(stderr, "cannot open DBC file: %s\n", dbcPath.c_str());
            return 1;
        }
        const rvn::CanTelemetryCodec codec(rvn::DbcDatabase::parse(dbcIn));

        rvn::SimConfig cfg;
        cfg.seed = seed;
        rvn::DriveSimulator sim(cfg);

        const auto steps = static_cast<long>(seconds * hz);
        const double dt = 1.0 / hz;

        std::ofstream outFile;
        std::unique_ptr<rvn::CanBus> bus;
        if (!outPath.empty()) {
            outFile.open(outPath);
            if (!outFile) {
                std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
                return 1;
            }
            outFile << "# reality-view-native candump v1 road_seed=" << seed << '\n';
        } else {
            bus = rvn::openSocketCan(canIface);
        }

        const auto period = std::chrono::nanoseconds(static_cast<long long>(1'000'000'000.0 / hz));
        auto next = std::chrono::steady_clock::now();
        std::size_t frameCount = 0;

        for (long i = 0; i < steps; ++i) {
            // Scripted drive: left lane change at 6 s, right lane change at 16 s, 55 mph at 22 s.
            if (i == static_cast<long>(6.0 * hz)) {
                sim.apply(rvn::cmd::SignalTurn{rvn::TurnSignal::Left});
            }
            if (i == static_cast<long>(16.0 * hz)) {
                sim.apply(rvn::cmd::SignalTurn{rvn::TurnSignal::Right});
            }
            if (i == static_cast<long>(22.0 * hz)) {
                sim.apply(rvn::cmd::SetCruiseSpeed{24.6F});
            }

            sim.step(dt);
            const rvn::TelemetrySample sample = sim.sample();
            const std::vector<rvn::CanFrame> frames = codec.encode(sample, seed);
            const double t = static_cast<double>(i) * dt;

            for (const rvn::CanFrame& f : frames) {
                if (outFile.is_open()) {
                    outFile << rvn::formatCandumpLine(t, logIface, f) << '\n';
                } else if (bus) {
                    (void)bus->send(f);
                }
                ++frameCount;
            }
            if (bus) {
                next += period;
                std::this_thread::sleep_until(next);
            }
        }

        std::printf("emitted %zu CAN frames (%.0f s at %.0f Hz)\n", frameCount, seconds, hz);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
