#include "vehicle_model.hpp"

#include <algorithm>
#include <cmath>

namespace rvn {

void VehicleModel::reset(const Pose2& pose, double speed) noexcept {
    state_ = KinematicState{};
    state_.pose = pose;
    state_.speed = speed;
}

void VehicleModel::step(const ActuatorInput& input, double dt) noexcept {
    if (dt <= 0.0) {
        return;
    }
    const double throttle = std::clamp(input.throttle, 0.0, 1.0);
    const double brake = std::clamp(input.brake, 0.0, 1.0);

    // Steering actuator: rate-limited, then clamped to the mechanical stop.
    const double target = std::clamp(input.roadWheelAngle, -params_.maxRoadWheelAngle, params_.maxRoadWheelAngle);
    const double maxDelta = params_.maxSteerRate * dt;
    state_.roadWheelAngle += std::clamp(target - state_.roadWheelAngle, -maxDelta, maxDelta);

    // Longitudinal: propulsion first, then every resisting force as one decel that pulls the
    // speed toward zero but never through it. That avoids the classic bug where braking at
    // low speed makes the car oscillate around 0 or start rolling backwards.
    const double v0 = state_.speed;
    double drive = 0.0;
    switch (input.gear) {
    case Gear::Drive: drive = throttle * params_.maxDriveAccel; break;
    case Gear::Reverse: drive = -0.5 * throttle * params_.maxDriveAccel; break;
    case Gear::Neutral: break;
    case Gear::Park: break;
    }
    double v = v0 + drive * dt;
    const double resist =
        (brake * params_.maxBrakeDecel + params_.rollingResistance + params_.dragCoefficient * v * v) * dt;
    v = (std::abs(v) <= resist) ? 0.0 : v - std::copysign(resist, v);
    if (input.gear == Gear::Park) {
        v = 0.0; // parking pawl; the simulator only allows Park near standstill
    }
    v = std::clamp(v, -params_.maxReverseSpeed, params_.maxForwardSpeed);

    state_.accel = (v - v0) / dt;
    state_.speed = v;

    // Lateral: yaw rate from bicycle geometry, integrated at the midpoint heading.
    state_.yawRate = v * std::tan(state_.roadWheelAngle) / params_.wheelbaseM;
    const double headingMid = state_.pose.heading + 0.5 * state_.yawRate * dt;
    state_.pose.position.x += v * std::cos(headingMid) * dt;
    state_.pose.position.y += v * std::sin(headingMid) * dt;
    state_.pose.heading = wrapAngle(state_.pose.heading + state_.yawRate * dt);
}

} // namespace rvn
