#pragma once

#include "idm.hpp"
#include "random.hpp"
#include "road.hpp"
#include "telemetry_source.hpp"
#include "vehicle_model.hpp"

#include <optional>
#include <span>
#include <vector>

namespace rvn {

struct SimConfig {
    std::uint64_t seed{7};
    int trafficCount{7};
    double cruiseSpeedMps{29.0}; ///< ~65 mph
    bool startEngaged{true};     ///< start rolling on Autopilot so the demo moves immediately
    GeoOrigin origin{};
    VehicleParams vehicle{};
    IdmParams accIdm{};
    IdmParams trafficIdm{};
};

/// Simulated drive: ego vehicle (manual or a simple driver-assist stack) plus background traffic
/// on a procedural highway. Deterministic for a given seed and step sequence.
class DriveSimulator final : public TelemetrySource {
public:
    struct Agent {
        std::uint32_t id{};
        double s{};
        int lane{};
        double speed{};
        double cruiseSpeed{};
        double phase{}; ///< drives a slow variation of the desired speed
    };

    explicit DriveSimulator(SimConfig config = {});

    void apply(const Command& command) override;
    void step(double dt) override;
    [[nodiscard]] TelemetrySample sample() const override;
    [[nodiscard]] std::uint64_t roadSeed() const override { return config_.seed; }

    // Introspection for tests and tools.
    [[nodiscard]] const KinematicState& ego() const noexcept { return model_.state(); }
    [[nodiscard]] const RoadProjection& egoOnRoad() const noexcept { return egoProjection_; }
    [[nodiscard]] AutopilotState autopilot() const noexcept;
    [[nodiscard]] int targetLane() const noexcept { return targetLane_; }
    [[nodiscard]] Gear gear() const noexcept { return gear_; }
    [[nodiscard]] TurnSignal turnSignal() const noexcept { return signal_; }
    [[nodiscard]] double cruiseSpeed() const noexcept { return cruiseSpeed_; }
    [[nodiscard]] double simTime() const noexcept { return simTime_; }
    [[nodiscard]] const Road& road() const noexcept { return road_; }
    [[nodiscard]] std::span<const Agent> traffic() const noexcept { return agents_; }
    /// Smallest bumper-to-bumper gap to any vehicle ahead in the ego's lane (nullopt if none).
    [[nodiscard]] std::optional<double> egoLeadGap() const;

private:
    void resetState();
    void stepTraffic(double dt);
    void respawnTraffic();
    void updateAutopilotAvailability();
    [[nodiscard]] ActuatorInput autopilotControl() const;
    [[nodiscard]] ActuatorInput manualControl() const;
    [[nodiscard]] std::optional<LeadVehicle> leadInLane(double s, int lane, std::optional<std::uint32_t> self) const;
    [[nodiscard]] int egoLane() const noexcept;

    SimConfig config_;
    Road road_;
    VehicleModel model_;
    SplitMix64 rng_;
    std::vector<Agent> agents_;
    RoadProjection egoProjection_{};

    cmd::ManualInput manual_{};
    Gear gear_{Gear::Drive};
    TurnSignal signal_{TurnSignal::Off};
    bool engaged_{false};
    bool available_{false};
    int targetLane_{1};
    double cruiseSpeed_{};
    double simTime_{};
    ActuatorInput lastActuation_{};
};

} // namespace rvn
