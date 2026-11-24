#include "process_stats.hpp"

#include <sys/resource.h>

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <unistd.h>
#endif

namespace rvn {
namespace {

double toSeconds(const timeval& tv) {
    return static_cast<double>(tv.tv_sec) + static_cast<double>(tv.tv_usec) * 1e-6;
}

} // namespace

ProcessUsage sampleProcessUsage() {
    ProcessUsage usage;
    rusage ru{};
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        usage.cpuSeconds = toSeconds(ru.ru_utime) + toSeconds(ru.ru_stime);
    }
#if defined(__APPLE__)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    // mach APIs take an int* "info" out-parameter; the cast is the documented usage.
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) ==
        KERN_SUCCESS) { // NOLINT
        usage.residentBytes = info.resident_size;
    }
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::uint64_t sizePages = 0;
    std::uint64_t residentPages = 0;
    if (statm >> sizePages >> residentPages) {
        usage.residentBytes = residentPages * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    }
#endif
    return usage;
}

double CpuMeter::update() {
    const auto now = std::chrono::steady_clock::now();
    const double cpu = sampleProcessUsage().cpuSeconds;
    double percent = 0.0;
    if (lastCpu_ >= 0.0) {
        const double wall = std::chrono::duration<double>(now - lastWall_).count();
        if (wall > 0.0) {
            percent = 100.0 * (cpu - lastCpu_) / wall;
        }
    }
    lastCpu_ = cpu;
    lastWall_ = now;
    return percent;
}

} // namespace rvn
