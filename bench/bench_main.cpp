// Micro-benchmarks for the hot paths. Dependency-free on purpose (no Google Benchmark): the
// harness is ~40 lines, and every number here is reproducible with one command:
//
//   cmake --build build --target rvn_bench && ./build/bench/rvn_bench
//
// Each benchmark runs 7 repetitions of a time-boxed batch and reports the MEDIAN ns/op, which
// is robust against the occasional scheduler hiccup that would poison a mean.
#include "drive_simulator.hpp"
#include "frame_stats.hpp"
#include "scene_geometry.hpp"
#include "spsc_ring_buffer.hpp"
#include "telemetry_consumer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace rvn;
using Clock = std::chrono::steady_clock;

namespace {

/// Prevents the optimizer from deleting a computation whose result is otherwise unused.
template <typename T> void doNotOptimize(const T& value) {
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : : "r,m"(value) : "memory");
#else
    static volatile const T* sink;
    sink = &value;
#endif
}

struct Result {
    std::string name;
    double nsPerOp;
    std::string note;
};
std::vector<Result> results;

void bench(const std::string& name, const std::function<void()>& op, const std::string& note = {}) {
    // Calibrate a batch size that takes ~20 ms.
    std::size_t batch = 1;
    while (true) {
        const auto t0 = Clock::now();
        for (std::size_t i = 0; i < batch; ++i)
            op();
        if (Clock::now() - t0 > std::chrono::milliseconds(20) || batch > (1U << 28U))
            break;
        batch *= 2;
    }
    std::vector<double> reps;
    for (int r = 0; r < 7; ++r) {
        const auto t0 = Clock::now();
        for (std::size_t i = 0; i < batch; ++i)
            op();
        reps.push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count() /
                       static_cast<double>(batch));
    }
    std::nth_element(reps.begin(), reps.begin() + 3, reps.end());
    results.push_back({name, reps[3], note});
}

/// Baseline for the lock-free queue: what most people write first.
template <typename T> class MutexQueue {
public:
    bool tryPush(const T& v) {
        std::lock_guard lock(m_);
        if (q_.size() >= cap_)
            return false;
        q_.push_back(v);
        return true;
    }
    bool tryPop(T& out) {
        std::lock_guard lock(m_);
        if (q_.empty())
            return false;
        out = q_.front();
        q_.pop_front();
        return true;
    }

private:
    std::mutex m_;
    std::deque<T> q_;
    std::size_t cap_{4096};
};

/// Two-thread throughput in millions of items per second.
template <typename Queue> double queueThroughputMops(Queue& q, std::uint64_t items) {
    const auto t0 = Clock::now();
    std::thread producer([&] {
        for (std::uint64_t i = 0; i < items;) {
            if (q.tryPush(i))
                ++i;
        }
    });
    std::uint64_t received = 0;
    std::uint64_t v = 0;
    while (received < items) {
        if (q.tryPop(v))
            ++received;
    }
    producer.join();
    const double s = std::chrono::duration<double>(Clock::now() - t0).count();
    return static_cast<double>(items) / s / 1e6;
}

} // namespace

int main() {
    std::printf("Reality View Native micro-benchmarks (median of 7)\n\n");

    // ---- Queues ---------------------------------------------------------------------------
    constexpr std::uint64_t kItems = 20'000'000;
    auto spsc = std::make_unique<SpscRingBuffer<std::uint64_t, 4096>>();
    const double spscMops = queueThroughputMops(*spsc, kItems);
    MutexQueue<std::uint64_t> mq;
    const double mutexMops = queueThroughputMops(mq, kItems / 4);

    // ---- Simulation -------------------------------------------------------------------------
    DriveSimulator sim;
    bench("DriveSimulator::step (7 agents, ACC + pure pursuit)", [&] { sim.step(1.0 / 240.0); });
    bench("DriveSimulator::sample (8 nearest objects)", [&] { doNotOptimize(sim.sample()); });

    Road road(7);
    road.ensureLength(20'000);
    const Vec2 p = road.pointAt(12'345.0, 2.0);
    bench("Road::project, hinted (+/-60 m window)", [&] { doNotOptimize(road.project(p, 12'340.0)); });
    bench(
        "Road::projectBruteForce (20 km road)", [&] { doNotOptimize(road.projectBruteForce(p)); },
        "why the hint matters");

    // ---- Pipeline ---------------------------------------------------------------------------
    {
        auto q = std::make_unique<TelemetryWorker::SampleQueue>();
        TelemetryConsumer consumer;
        TelemetrySample s = sim.sample();
        std::uint64_t seq = 0;
        bench(
            "Consumer drain of 17 samples (1 kHz into 60 Hz)",
            [&] {
                for (int i = 0; i < 17; ++i) {
                    s.seq = seq++;
                    s.x += 0.03;
                    (void)q->tryPush(s);
                }
                consumer.drain(*q);
            },
            "includes the 17 pushes");
    }

    // ---- Geometry ---------------------------------------------------------------------------
    std::vector<ColoredVertex> verts;
    verts.reserve(64 * 1024);
    std::size_t roadVerts = 0;
    bench(
        "buildRoadMesh, 800 m window",
        [&] {
            verts.clear();
            buildRoadMesh(road, 10'000.0, 10'800.0, road.at(10'250.0).position, verts);
            roadVerts = verts.size();
        },
        "rebuilt only every 150 m");
    std::vector<Vec2> trail;
    for (int i = 0; i < 2048; ++i)
        trail.push_back(road.pointAt(1000.0 + i * 0.5, 0.0));
    bench("buildTrailMesh, 2048 points", [&] {
        verts.clear();
        buildTrailMesh(trail, 0.5, trail.back(), verts);
    });
    const TelemetrySample frame = sim.sample();
    bench(
        "buildTrackedObjectsMesh, 8 cars + ego",
        [&] {
            verts.clear();
            buildTrackedObjectsMesh(std::span<const TrackedObject>(frame.objects.data(), frame.objectCount), {}, verts);
            MeshBuilder mb(verts, {});
            appendVehicleMesh(mb, {}, VehicleAppearance{});
        },
        "per frame");

    FrameStats fs(240);
    for (int i = 0; i < 240; ++i)
        fs.addFrame(8.0 + (i % 7));
    bench("FrameStats::summary (240-frame window)", [&] { doNotOptimize(fs.summary()); }, "4x per second");

    std::printf("| Benchmark | Time | Notes |\n|---|---:|---|\n");
    for (const auto& r : results) {
        if (r.nsPerOp >= 1000.0) {
            std::printf("| %s | %.2f us | %s |\n", r.name.c_str(), r.nsPerOp / 1000.0, r.note.c_str());
        } else {
            std::printf("| %s | %.1f ns | %s |\n", r.name.c_str(), r.nsPerOp, r.note.c_str());
        }
    }
    std::printf("| SPSC ring buffer, 2 threads | %.1f M items/s | lock-free, cached indices |\n", spscMops);
    std::printf("| std::mutex + std::deque, 2 threads | %.1f M items/s | baseline (%.1fx slower) |\n", mutexMops,
                spscMops / mutexMops);
    std::printf("\nRoad mesh: %zu vertices (%.0f KB) per rebuild\n", roadVerts,
                static_cast<double>(roadVerts * sizeof(ColoredVertex)) / 1024.0);
    std::printf("sizeof(TelemetrySample) = %zu bytes; sample queue = %.1f MB\n", sizeof(TelemetrySample),
                static_cast<double>(sizeof(TelemetryWorker::SampleQueue)) / (1024.0 * 1024.0));
    return 0;
}
