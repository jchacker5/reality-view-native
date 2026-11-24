#pragma once

#include <cstdint>

namespace rvn {

/// SplitMix64: tiny, fast and bit-identical on every platform and standard library, which
/// std::uniform_*_distribution is not. Determinism matters here: tests pin behaviour to a
/// seed, and the UI rebuilds the simulator's road from the same seed.
class SplitMix64 {
public:
    explicit constexpr SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

    constexpr std::uint64_t next() noexcept {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }

    /// Uniform double in [lo, hi).
    constexpr double uniform(double lo, double hi) noexcept {
        const double unit = static_cast<double>(next() >> 11U) * 0x1.0p-53;
        return lo + (hi - lo) * unit;
    }

    /// Uniform int in [lo, hi]. Slight modulo bias is irrelevant at these ranges.
    constexpr int uniformInt(int lo, int hi) noexcept {
        const auto span = static_cast<std::uint64_t>(hi - lo + 1);
        return lo + static_cast<int>(next() % span);
    }

private:
    std::uint64_t state_;
};

} // namespace rvn
