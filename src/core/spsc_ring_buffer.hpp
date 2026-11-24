#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <limits>
#include <type_traits>

namespace rvn {

/// Cache-line size used for padding. 128 rather than 64: Apple silicon and some x86 parts
/// prefetch adjacent line pairs, so 64-byte padding can still false-share.
inline constexpr std::size_t kCacheLine = 128;

/// Bounded, wait-free, single-producer / single-consumer ring buffer.
///
/// Design notes (see docs/ARCHITECTURE.md for the interview-length version):
///  * Exactly one thread may call the producer API (tryPush), and exactly one other thread
///    the consumer API (tryPop / drain). That restriction is what makes it lock-free with
///    nothing stronger than acquire/release ordering.
///  * head_ and tail_ are free-running counters; the slot is `counter & kMask`. Unsigned
///    wrap-around keeps `head - tail` correct even after 2^64 operations.
///  * Each side caches the other side's counter, so the hot path touches only its own cache
///    line and refreshes the shared one only when the queue looks full (producer) or empty
///    (consumer). This removes most cross-core cache traffic.
///  * When full, tryPush fails instead of overwriting: the producer decides the policy
///    (we count the overflow and drop the newest sample; telemetry consumers prefer a
///    sequence gap they can detect over silently reordered data).
template <typename T, std::size_t Capacity> class SpscRingBuffer {
    static_assert(Capacity >= 2 && std::has_single_bit(Capacity), "Capacity must be a power of two");
    static_assert(std::is_nothrow_copy_assignable_v<T>);
    static_assert(std::is_nothrow_default_constructible_v<T>);

public:
    using value_type = T;
    static constexpr std::size_t kCapacity = Capacity;

    SpscRingBuffer() = default;
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // ---- producer side --------------------------------------------------------------
    [[nodiscard]] bool tryPush(const T& value) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head - cachedTail_ >= Capacity) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (head - cachedTail_ >= Capacity) {
                return false;
            }
        }
        slots_[head & kMask] = value;
        head_.store(head + 1, std::memory_order_release); // publish the slot write
        return true;
    }

    // ---- consumer side --------------------------------------------------------------
    [[nodiscard]] bool tryPop(T& out) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == cachedHead_) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (tail == cachedHead_) {
                return false;
            }
        }
        out = slots_[tail & kMask];
        tail_.store(tail + 1, std::memory_order_release); // hand the slot back
        return true;
    }

    /// Consume up to `maxItems` items, calling `fn(const T&)` on each, with a single release
    /// store at the end. Batching is how the UI thread catches up on a burst of telemetry in
    /// one frame without paying an atomic store per sample.
    template <typename Fn> std::size_t drain(Fn&& fn, std::size_t maxItems = std::numeric_limits<std::size_t>::max()) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        cachedHead_ = head_.load(std::memory_order_acquire);
        std::size_t available = cachedHead_ - tail;
        if (available > maxItems) {
            available = maxItems;
        }
        for (std::size_t i = 0; i < available; ++i) {
            fn(static_cast<const T&>(slots_[(tail + i) & kMask]));
        }
        if (available != 0) {
            tail_.store(tail + available, std::memory_order_release);
        }
        return available;
    }

    /// Approximate: exact only when called from a quiescent state. Safe from either side.
    [[nodiscard]] std::size_t sizeApprox() const noexcept {
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        const std::size_t head = head_.load(std::memory_order_acquire);
        return head - tail;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // Producer-owned line.
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};
    std::size_t cachedTail_{0};
    // Consumer-owned line.
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
    std::size_t cachedHead_{0};
    // Storage starts on its own line so slot writes never invalidate the counters.
    alignas(kCacheLine) std::array<T, Capacity> slots_{};
};

} // namespace rvn
