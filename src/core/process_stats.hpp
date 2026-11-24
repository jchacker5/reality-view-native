#pragma once

#include <chrono>
#include <cstdint>

namespace rvn {

struct ProcessUsage {
    double cpuSeconds{};           ///< user + system CPU time consumed so far, all threads
    std::uint64_t residentBytes{}; ///< current resident set size
};

/// Platform-specific (macOS: getrusage + mach task_info; Linux: getrusage + /proc/self/statm).
[[nodiscard]] ProcessUsage sampleProcessUsage();

/// Converts successive ProcessUsage samples into a CPU percentage (100% = one core busy).
class CpuMeter {
public:
    /// Returns the CPU % since the previous call (0 on the first call).
    double update();

private:
    std::chrono::steady_clock::time_point lastWall_{};
    double lastCpu_{-1.0};
};

} // namespace rvn
