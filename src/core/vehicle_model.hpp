#pragma once

#include "geometry.hpp"
#include "telemetry.hpp"

namespace rvn {

/// Physical parameters. Defaults are in the neighbourhood of a mid-size EV sedan.
struct VehicleParams {
    double wheelbaseM{2.875};
    double lengthM{4.72};
    double widthM{1.85};
    double maxDriveAccel{4.5};       ///< m/s^2 at full throttle
    double maxBrakeDecel{9.0};       ///< m/s^2 at full brake
    double rollingResistance{0.12};  ///< m/s^2
    double dragCoefficient{0.00042}; ///< a_drag = c * v^2
    double maxRoadWheelAngle{0.61};  ///< rad (~35 degrees)
    double maxSteerRate{1.0};        ///< rad/s at the road wheels
    double steeringRatio{15.0};      ///< steering-wheel angle / road-wheel angle
    double maxForwardSpeed{69.0};    ///< m/s (~155 mph)
    double maxReverseSpeed{5.0};
};

struct KinematicState {
    Pose2 pose{};
    double speed{}; ///< signed: negative while reversing
    double accel{};
    double roadWheelAngle{};
    double yawRate{};
};

struct ActuatorInput {
    double throttle{};       ///< 0..1
    double brake{};          ///< 0..1
    double roadWheelAngle{}; ///< commanded, rad; the model rate-limits it
    Gear gear{Gear::Drive};
};

/// Kinematic bicycle model. Good enough for visualization: no tyre slip, which is fine below
/// roughly 0.5 g of lateral acceleration. Integration is semi-implicit Euler at a fixed step.
class VehicleModel {
public:
    explicit VehicleModel(VehicleParams params = {}) noexcept : params_(params) {}

    void reset(const Pose2& pose, double speed = 0.0) noexcept;
    void step(const ActuatorInput& input, double dt) noexcept;

    [[nodiscard]] const KinematicState& state() const noexcept { return state_; }
    [[nodiscard]] const VehicleParams& params() const noexcept { return params_; }

private:
    VehicleParams params_;
    KinematicState state_{};
};

} // namespace rvn
