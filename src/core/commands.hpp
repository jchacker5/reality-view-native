#pragma once

#include "telemetry.hpp"

#include <variant>

namespace rvn {

/// Commands flow UI -> worker through their own SPSC queue. A closed set of value types in a
/// std::variant: no allocation, no inheritance, and std::visit makes forgetting a case a
/// compile error (with the `Overloaded` helper below).
namespace cmd {
struct ManualInput {
    float throttle{}; ///< 0..1
    float brake{};    ///< 0..1
    float steer{};    ///< -1 (full right) .. +1 (full left)
};
struct ShiftGear {
    Gear gear{};
};
struct SignalTurn {
    TurnSignal signal{};
};
struct SetAutopilot {
    bool engaged{};
};
struct SetCruiseSpeed {
    float mps{};
};
struct Reset {};

// Pipeline (worker-level) commands, handled by the worker rather than the source.
struct SetPublishRate {
    int hz{};
};
struct InjectStall {
    int milliseconds{}; ///< blocks the producer thread to simulate a slow backend
};
struct SetDropout {
    bool enabled{}; ///< drop ~30% of samples at the producer to exercise gap handling
};
} // namespace cmd

using Command = std::variant<cmd::ManualInput, cmd::ShiftGear, cmd::SignalTurn, cmd::SetAutopilot, cmd::SetCruiseSpeed,
                             cmd::Reset, cmd::SetPublishRate, cmd::InjectStall, cmd::SetDropout>;

template <typename... Fs> struct Overloaded : Fs... {
    using Fs::operator()...;
};
template <typename... Fs> Overloaded(Fs...) -> Overloaded<Fs...>;

} // namespace rvn
