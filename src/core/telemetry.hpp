#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace rvn {

enum class Gear : std::uint8_t { Park, Reverse, Neutral, Drive };
enum class TurnSignal : std::uint8_t { Off, Left, Right };
enum class AutopilotState : std::uint8_t { Unavailable, Standby, Active };

/// Another road user the simulated perception stack is tracking.
struct TrackedObject {
    std::uint32_t id{};
    double x{};      ///< world east, metres
    double y{};      ///< world north, metres
    float heading{}; ///< radians, counter-clockwise from +x
    float speedMps{};
    float length{4.7F};
    float width{1.9F};
};

inline constexpr std::size_t kMaxTrackedObjects = 8;

/// One telemetry frame. Trivially copyable on purpose: it travels through a lock-free
/// queue by value, so it must not own heap memory.
///
/// Positions are double precision. Far from the origin a float loses centimetres
/// (float has 24 mantissa bits: at 100 km the step is ~8 mm and grows from there).
/// The renderer re-bases everything to a nearby "floating origin" before narrowing to float.
struct TelemetrySample {
    std::uint64_t seq{};        ///< monotonically increasing; gaps mean dropped samples
    std::int64_t timestampNs{}; ///< producer steady-clock time
    double simTimeS{};

    double x{};
    double y{};
    double latitude{};
    double longitude{};
    float heading{};
    float speedMps{};
    float accelMps2{};
    float yawRateRadS{};
    float steeringWheelDeg{};
    float throttle{}; ///< 0..1
    float brake{};    ///< 0..1
    float setSpeedMps{};
    float laneOffsetM{}; ///< lateral offset from the current lane centre, + is left
    float roadS{};       ///< arc length along the road centreline
    std::int8_t lane{};  ///< 0 is the rightmost lane

    Gear gear{Gear::Park};
    TurnSignal turnSignal{TurnSignal::Off};
    AutopilotState autopilot{AutopilotState::Unavailable};

    std::uint8_t objectCount{};
    std::array<TrackedObject, kMaxTrackedObjects> objects{};
};

static_assert(std::is_trivially_copyable_v<TelemetrySample>,
              "TelemetrySample crosses threads through a lock-free queue by value");

constexpr std::string_view label(Gear g) noexcept {
    switch (g) {
    case Gear::Park: return "P";
    case Gear::Reverse: return "R";
    case Gear::Neutral: return "N";
    case Gear::Drive: return "D";
    }
    return "?";
}

constexpr std::string_view label(AutopilotState s) noexcept {
    switch (s) {
    case AutopilotState::Unavailable: return "Unavailable";
    case AutopilotState::Standby: return "Standby";
    case AutopilotState::Active: return "Active";
    }
    return "?";
}

constexpr std::string_view label(TurnSignal s) noexcept {
    switch (s) {
    case TurnSignal::Off: return "Off";
    case TurnSignal::Left: return "Left";
    case TurnSignal::Right: return "Right";
    }
    return "?";
}

inline constexpr double kMpsToMph = 2.2369362920544;

} // namespace rvn
