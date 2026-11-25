#pragma once

#include "can_bus.hpp"
#include "dbc.hpp"
#include "geometry.hpp"
#include "telemetry_source.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace rvn {

/// Pre-resolved signal mapper between a DbcDatabase and TelemetrySample.
/// Resolves canonical vehicle signal names at construction so encode/decode does zero allocations.
class CanTelemetryCodec {
public:
    explicit CanTelemetryCodec(DbcDatabase db);

    /// Encodes a TelemetrySample (and roadSeed) into all matching DBC frames.
    [[nodiscard]] std::vector<CanFrame> encode(const TelemetrySample& sample, std::uint64_t roadSeed = 7) const;

    /// Decodes a single CAN frame, updating only the fields carried by that message ID.
    /// Returns true if the message ID is known in the DBC.
    bool decodeInto(const CanFrame& frame, TelemetrySample& sample, std::uint64_t& roadSeed,
                    const GeoOrigin& origin = {}) const noexcept;

    [[nodiscard]] const DbcDatabase& database() const noexcept { return db_; }

private:
    enum class Field : std::uint8_t {
        VehicleSpeed,
        LongitudinalAccel,
        YawRate,
        GearState,
        RoadSeed,
        SteeringWheelAngle,
        ThrottlePos,
        BrakePos,
        TurnSignalState,
        AutopilotState,
        CruiseSetSpeed,
        LaneIndex,
        LaneOffset,
        RoadArcLength,
        WorldPosX,
        WorldPosY,
        VehicleHeading,
    };

    struct BoundSignal {
        Field field;
        DbcSignal signal;
    };

    struct BoundMessage {
        std::uint32_t id{};
        bool extended{false};
        std::uint8_t dlc{8};
        std::vector<BoundSignal> fields;
    };

    DbcDatabase db_;
    std::vector<BoundMessage> messages_;
};

/// TelemetrySource backed by a CAN bus (live Linux SocketCAN / MemoryCanBus) or a recorded
/// candump log replayed through a DBC database.
class CanTelemetrySource final : public TelemetrySource {
public:
    /// Live bus constructor.
    CanTelemetrySource(DbcDatabase db, std::unique_ptr<CanBus> bus, std::uint64_t defaultSeed = 7,
                       GeoOrigin origin = {});

    /// Recorded candump log constructor (loops at end, same contract as ReplaySource).
    /// Throws std::invalid_argument if `entries` is empty.
    CanTelemetrySource(DbcDatabase db, std::vector<CandumpEntry> entries, std::uint64_t defaultSeed = 7,
                       GeoOrigin origin = {});

    void apply(const Command& command) override;
    void step(double dt) override;
    [[nodiscard]] TelemetrySample sample() const override;
    [[nodiscard]] std::uint64_t roadSeed() const override { return roadSeed_; }

private:
    CanTelemetryCodec codec_;
    std::unique_ptr<CanBus> bus_;
    std::vector<CandumpEntry> log_;
    std::size_t logCursor_{0};
    double playhead_{0.0};
    double logDuration_{0.0};
    std::uint64_t defaultSeed_{7};
    std::uint64_t roadSeed_{7};
    GeoOrigin origin_{};
    TelemetrySample current_{};
    double simTime_{0.0};
};

} // namespace rvn
