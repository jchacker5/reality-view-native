#pragma once

#include "commands.hpp"
#include "telemetry.hpp"

#include <cstdint>

namespace rvn {

/// Anything that can produce telemetry on the worker thread: the live simulator, or a
/// replay of a recorded CSV. The worker owns exactly one through a unique_ptr.
class TelemetrySource {
public:
    TelemetrySource() = default;
    virtual ~TelemetrySource() = default;
    TelemetrySource(const TelemetrySource&) = delete;
    TelemetrySource& operator=(const TelemetrySource&) = delete;
    TelemetrySource(TelemetrySource&&) = delete;
    TelemetrySource& operator=(TelemetrySource&&) = delete;

    /// Vehicle-level commands (pipeline commands never reach the source).
    virtual void apply(const Command& command) = 0;
    /// Advance by a fixed step. Called at the publish rate.
    virtual void step(double dt) = 0;
    /// Snapshot the current state. The worker assigns seq and timestampNs.
    [[nodiscard]] virtual TelemetrySample sample() const = 0;
    /// Seed of the road this source drives on; the renderer rebuilds the same road from it.
    [[nodiscard]] virtual std::uint64_t roadSeed() const = 0;
};

} // namespace rvn
