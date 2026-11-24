#include "telemetry_worker.hpp"

#include <algorithm>
#include <chrono>

namespace rvn {

using Clock = std::chrono::steady_clock;

TelemetryWorker::TelemetryWorker(std::unique_ptr<TelemetrySource> source, int publishRateHz)
    : source_(std::move(source)), roadSeed_(source_->roadSeed()), samples_(std::make_unique<SampleQueue>()),
      commands_(std::make_unique<CommandQueue>()), rateHz_(std::clamp(publishRateHz, kMinRateHz, kMaxRateHz)) {}

TelemetryWorker::~TelemetryWorker() {
    stop();
}

void TelemetryWorker::start() {
    if (thread_.joinable()) {
        return;
    }
    stopRequested_.store(false, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void TelemetryWorker::stop() {
    if (!thread_.joinable()) {
        return;
    }
    {
        const std::lock_guard lock(sleepMutex_);
        stopRequested_.store(true, std::memory_order_release);
    }
    sleepCv_.notify_all(); // wakes a sleeping or stalled worker immediately
    thread_.join();
}

bool TelemetryWorker::post(const Command& command) {
    return commands_->tryPush(command);
}

void TelemetryWorker::sleepUntil(Clock::time_point deadline) {
    std::unique_lock lock(sleepMutex_);
    // Returns at the deadline, or as soon as stop() is called (spurious wake-ups are re-checked).
    sleepCv_.wait_until(lock, deadline, [this] { return stopRequested(); });
}

void TelemetryWorker::run() {
    std::uint64_t seq = 0;
    std::uint64_t dropoutCounter = 0;
    bool dropout = false;
    auto next = Clock::now();

    while (!stopRequested()) {
        // 1. Apply every pending command. Pipeline commands are handled here; vehicle commands
        //    go to the source.
        Command command;
        while (commands_->tryPop(command)) {
            std::visit(Overloaded{
                           [this](const cmd::SetPublishRate& r) {
                               rateHz_.store(std::clamp(r.hz, kMinRateHz, kMaxRateHz), std::memory_order_relaxed);
                           },
                           [this](const cmd::InjectStall& s) {
                               // Deliberately block this thread, as a slow backend or a long GC
                               // pause would. The UI must keep rendering at full rate meanwhile.
                               stalled_.store(true, std::memory_order_relaxed);
                               sleepUntil(Clock::now() + std::chrono::milliseconds(s.milliseconds));
                               stalled_.store(false, std::memory_order_relaxed);
                           },
                           [&dropout](const cmd::SetDropout& d) { dropout = d.enabled; },
                           [this](const auto& vehicleCommand) { source_->apply(vehicleCommand); },
                       },
                       command);
        }
        if (stopRequested()) {
            break;
        }

        // 2. Fixed-step simulation: dt is the nominal period, not the measured one, so the
        //    simulation is deterministic regardless of scheduling jitter.
        const int hz = rateHz_.load(std::memory_order_relaxed);
        const auto period = std::chrono::nanoseconds(1'000'000'000LL / hz);
        source_->step(1.0 / static_cast<double>(hz));

        TelemetrySample s = source_->sample();
        s.seq = seq++;
        s.timestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        // Dropout mode loses 3 of every 10 samples: the consumer must notice via seq gaps.
        const bool drop = dropout && (dropoutCounter++ % 10) < 3;
        if (!drop) {
            if (samples_->tryPush(s)) {
                produced_.fetch_add(1, std::memory_order_relaxed);
            } else {
                overflows_.fetch_add(1, std::memory_order_relaxed); // consumer is not keeping up
            }
        }

        // 3. Pace with an absolute deadline (sleep_until, not sleep_for) so per-iteration work
        //    does not accumulate as drift. If we fell far behind (after a stall), resynchronize
        //    instead of bursting to catch up: a burst of stale samples is worse than a gap.
        next += period;
        const auto now = Clock::now();
        if (now - next > std::chrono::milliseconds(100)) {
            next = now;
        }
        sleepUntil(next);
    }
}

} // namespace rvn
