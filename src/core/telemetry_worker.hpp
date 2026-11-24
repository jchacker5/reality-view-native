#pragma once

#include "commands.hpp"
#include "spsc_ring_buffer.hpp"
#include "telemetry.hpp"
#include "telemetry_source.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace rvn {

/// Owns the producer thread. The source (simulator or replay) lives entirely on that thread;
/// the only shared state is two SPSC queues and a handful of atomics, so there is no mutex on
/// any per-sample path.
///
///   UI thread --post(Command)--> [command queue] --> worker: apply, step, sample
///   UI thread <--drain()-------- [sample queue]  <-- worker: tryPush (drop + count if full)
class TelemetryWorker {
public:
    static constexpr std::size_t kSampleQueueCapacity = 4096;
    static constexpr std::size_t kCommandQueueCapacity = 256;
    using SampleQueue = SpscRingBuffer<TelemetrySample, kSampleQueueCapacity>;
    using CommandQueue = SpscRingBuffer<Command, kCommandQueueCapacity>;

    TelemetryWorker(std::unique_ptr<TelemetrySource> source, int publishRateHz);
    ~TelemetryWorker(); ///< requests stop and joins (RAII: no thread outlives its owner)

    TelemetryWorker(const TelemetryWorker&) = delete;
    TelemetryWorker& operator=(const TelemetryWorker&) = delete;
    TelemetryWorker(TelemetryWorker&&) = delete;
    TelemetryWorker& operator=(TelemetryWorker&&) = delete;

    void start();
    void stop(); ///< returns promptly even mid-stall: the sleep is interruptible

    /// UI thread only. Returns false if the command queue is full.
    bool post(const Command& command);

    /// Consumer side of the sample queue. UI thread only.
    [[nodiscard]] SampleQueue& samples() noexcept { return *samples_; }

    [[nodiscard]] std::uint64_t roadSeed() const noexcept { return roadSeed_; }
    [[nodiscard]] int publishRateHz() const noexcept { return rateHz_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t overflowCount() const noexcept { return overflows_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t producedCount() const noexcept { return produced_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool stalled() const noexcept { return stalled_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }

    static constexpr int kMinRateHz = 10;
    static constexpr int kMaxRateHz = 4000;

private:
    void run(const std::stop_token& stop);
    /// Sleep until `deadline` or until stop is requested, whichever comes first.
    void sleepUntil(const std::stop_token& stop, std::chrono::steady_clock::time_point deadline);

    std::unique_ptr<TelemetrySource> source_;
    std::uint64_t roadSeed_;
    // Heap-allocated: the sample queue is ~2 MB, too big for the stack of whoever owns us.
    std::unique_ptr<SampleQueue> samples_;
    std::unique_ptr<CommandQueue> commands_;

    std::atomic<int> rateHz_;
    std::atomic<std::uint64_t> overflows_{0};
    std::atomic<std::uint64_t> produced_{0};
    std::atomic<bool> stalled_{false};

    std::mutex sleepMutex_; // only guards the condition variable; never held while working
    std::condition_variable_any sleepCv_;
    std::jthread thread_;
};

} // namespace rvn
