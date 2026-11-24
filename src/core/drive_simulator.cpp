#include "drive_simulator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rvn {
namespace {

constexpr double kRadToDeg = 180.0 / std::numbers::pi;
constexpr double kAgentLength = 4.7;
constexpr double kStartS = 40.0;
constexpr double kTrafficAheadM = 450.0;
constexpr double kTrafficBehindM = 200.0;
constexpr double kObjectRangeM = 160.0;

} // namespace

DriveSimulator::DriveSimulator(SimConfig config)
    : config_(config), road_(config.seed), model_(config.vehicle), rng_(config.seed ^ 0xA5A5A5A5ULL) {
    resetState();
}

void DriveSimulator::resetState() {
    rng_ = SplitMix64(config_.seed ^ 0xA5A5A5A5ULL);
    const int middleLane = road_.params().laneCount / 2;
    targetLane_ = middleLane;
    const Vec2 start = road_.pointAt(kStartS, road_.laneCenterOffset(middleLane));
    const double startSpeed = config_.startEngaged ? config_.cruiseSpeedMps * 0.85 : 0.0;
    model_.reset(Pose2{start, road_.at(kStartS).heading}, startSpeed);
    egoProjection_ = road_.project(start, kStartS);
    gear_ = config_.startEngaged ? Gear::Drive : Gear::Park;
    signal_ = TurnSignal::Off;
    manual_ = {};
    cruiseSpeed_ = config_.cruiseSpeedMps;
    simTime_ = 0.0;
    lastActuation_ = {};

    agents_.clear();
    const int lanes = road_.params().laneCount;
    for (int i = 0; i < config_.trafficCount; ++i) {
        Agent a;
        a.id = static_cast<std::uint32_t>(i + 1);
        a.lane = i % lanes;
        // Spread agents along the road; keep the ego's own lane clear for the first 80 m.
        a.s = kStartS + 80.0 + static_cast<double>(i) * 55.0 + rng_.uniform(0.0, 20.0);
        a.cruiseSpeed = rng_.uniform(24.0, 31.0) + (a.lane == lanes - 1 ? 2.5 : 0.0);
        a.speed = a.cruiseSpeed * 0.9;
        a.phase = rng_.uniform(0.0, 2.0 * std::numbers::pi);
        agents_.push_back(a);
    }
    updateAutopilotAvailability();
    engaged_ = config_.startEngaged && available_;
}

AutopilotState DriveSimulator::autopilot() const noexcept {
    if (engaged_) {
        return AutopilotState::Active;
    }
    return available_ ? AutopilotState::Standby : AutopilotState::Unavailable;
}

int DriveSimulator::egoLane() const noexcept {
    return road_.laneAt(egoProjection_.lateral);
}

void DriveSimulator::apply(const Command& command) {
    std::visit(
        Overloaded{
            [this](const cmd::ManualInput& in) {
                manual_ = in;
                // Driver override: a firm brake or a deliberate steer disengages, like a real L2 system.
                if (engaged_ && (in.brake > 0.2F || std::abs(in.steer) > 0.5F)) {
                    engaged_ = false;
                }
            },
            [this](const cmd::ShiftGear& g) {
                const double v = std::abs(model_.state().speed);
                const bool allowed =
                    (g.gear == Gear::Park) ? v < 0.3 : (g.gear == Gear::Neutral || v < 1.0 || g.gear == gear_);
                if (allowed) {
                    gear_ = g.gear;
                    // Recompute now: availability must not lag a gear change by a tick.
                    updateAutopilotAvailability();
                }
            },
            [this](const cmd::SignalTurn& t) { signal_ = t.signal; },
            [this](const cmd::SetAutopilot& ap) {
                if (!ap.engaged) {
                    engaged_ = false;
                    return;
                }
                updateAutopilotAvailability();
                if (available_ && !engaged_) {
                    engaged_ = true;
                    targetLane_ = egoLane();
                    const double v = model_.state().speed;
                    if (v > 8.0) {
                        cruiseSpeed_ = v; // set speed follows the current speed, as in most cars
                    }
                }
            },
            [this](const cmd::SetCruiseSpeed& c) { cruiseSpeed_ = std::clamp(static_cast<double>(c.mps), 0.0, 40.0); },
            [this](const cmd::Reset&) { resetState(); },
            // Pipeline commands are the worker's business.
            [](const cmd::SetPublishRate&) {},
            [](const cmd::InjectStall&) {},
            [](const cmd::SetDropout&) {},
        },
        command);
}

void DriveSimulator::updateAutopilotAvailability() {
    const bool onRoad = std::abs(egoProjection_.lateral) < road_.halfWidth() - 0.5;
    const double headingError = std::abs(wrapAngle(model_.state().pose.heading - egoProjection_.heading));
    available_ = gear_ == Gear::Drive && onRoad && headingError < 0.5 && model_.state().speed >= 0.0;
    if (!available_) {
        engaged_ = false;
    }
}

std::optional<LeadVehicle> DriveSimulator::leadInLane(double s, int lane, std::optional<std::uint32_t> self) const {
    std::optional<LeadVehicle> best;
    auto consider = [&](double otherS, double otherSpeed) {
        const double gap = otherS - s - kAgentLength;
        if (otherS > s && (!best || gap < best->gapM)) {
            best = LeadVehicle{gap, otherSpeed};
        }
    };
    for (const Agent& a : agents_) {
        if (a.lane == lane && (!self || a.id != *self)) {
            consider(a.s, a.speed);
        }
    }
    if (self && egoLane() == lane) {
        consider(egoProjection_.s, model_.state().speed); // traffic also respects the ego car
    }
    return best;
}

std::optional<double> DriveSimulator::egoLeadGap() const {
    const auto lead = leadInLane(egoProjection_.s, egoLane(), std::nullopt);
    return lead ? std::optional<double>(lead->gapM) : std::nullopt;
}

ActuatorInput DriveSimulator::autopilotControl() const {
    const KinematicState& ego = model_.state();
    const VehicleParams& vp = model_.params();
    const double v = std::max(ego.speed, 0.0);

    // Longitudinal: IDM against the closest lead in the current and target lanes (during a
    // lane change both matter).
    auto lead = leadInLane(egoProjection_.s, egoLane(), std::nullopt);
    if (targetLane_ != egoLane()) {
        const auto other = leadInLane(egoProjection_.s, targetLane_, std::nullopt);
        if (other && (!lead || other->gapM < lead->gapM)) {
            lead = other;
        }
    }
    const double accelCmd =
        std::clamp(idmAcceleration(config_.accIdm, v, cruiseSpeed_, lead), -0.7 * vp.maxBrakeDecel, vp.maxDriveAccel);

    ActuatorInput out;
    out.gear = Gear::Drive;
    if (accelCmd >= 0.0) {
        const double resist = vp.rollingResistance + vp.dragCoefficient * v * v;
        out.throttle = std::clamp((accelCmd + resist) / vp.maxDriveAccel, 0.0, 1.0);
    } else {
        out.brake = std::clamp(-accelCmd / vp.maxBrakeDecel, 0.0, 1.0);
    }

    // Lateral: pure pursuit toward the target lane centre at a speed-dependent lookahead.
    const double lookahead = std::clamp(6.0 + 0.9 * v, 10.0, 45.0);
    const Vec2 target = road_.pointAt(egoProjection_.s + lookahead, road_.laneCenterOffset(targetLane_));
    const Vec2 local = ego.pose.toLocal(target);
    const double alpha = std::atan2(local.y, local.x);
    out.roadWheelAngle = std::atan2(2.0 * vp.wheelbaseM * std::sin(alpha), lookahead);
    return out;
}

ActuatorInput DriveSimulator::manualControl() const {
    const double v = std::abs(model_.state().speed);
    // Speed-sensitive steering so keyboard input stays drivable at highway speed.
    const double scale = 1.0 / (1.0 + (v / 9.0) * (v / 9.0));
    return ActuatorInput{static_cast<double>(manual_.throttle), static_cast<double>(manual_.brake),
                         static_cast<double>(manual_.steer) * model_.params().maxRoadWheelAngle * scale, gear_};
}

void DriveSimulator::step(double dt) {
    if (dt <= 0.0) {
        return;
    }
    simTime_ += dt;

    // Automatic lane change on turn signal while engaged.
    const int lanes = road_.params().laneCount;
    if (engaged_ && targetLane_ == egoLane()) {
        if (signal_ == TurnSignal::Left && targetLane_ < lanes - 1) {
            ++targetLane_;
        } else if (signal_ == TurnSignal::Right && targetLane_ > 0) {
            --targetLane_;
        }
    }

    lastActuation_ = engaged_ ? autopilotControl() : manualControl();
    model_.step(lastActuation_, dt);

    road_.ensureLength(egoProjection_.s + 1500.0);
    egoProjection_ = road_.project(model_.state().pose.position, egoProjection_.s);
    updateAutopilotAvailability();

    // Cancel the signal once the lane change has completed.
    if (signal_ != TurnSignal::Off && engaged_ &&
        std::abs(egoProjection_.lateral - road_.laneCenterOffset(targetLane_)) < 0.3) {
        signal_ = TurnSignal::Off;
    }

    stepTraffic(dt);
}

void DriveSimulator::stepTraffic(double dt) {
    for (Agent& a : agents_) {
        const double desired = a.cruiseSpeed + 2.0 * std::sin(0.05 * simTime_ + a.phase);
        const double accel = idmAcceleration(config_.trafficIdm, a.speed, desired, leadInLane(a.s, a.lane, a.id));
        a.speed = std::max(0.0, a.speed + accel * dt);
        a.s += a.speed * dt;
    }
    respawnTraffic();
}

void DriveSimulator::respawnTraffic() {
    const double egoS = egoProjection_.s;
    for (Agent& a : agents_) {
        double newS = a.s;
        if (a.s < egoS - kTrafficBehindM) {
            newS = egoS + rng_.uniform(250.0, kTrafficAheadM);
        } else if (a.s > egoS + kTrafficAheadM + 150.0) {
            newS = egoS - rng_.uniform(100.0, kTrafficBehindM - 20.0);
        } else {
            continue;
        }
        const int lane = rng_.uniformInt(0, road_.params().laneCount - 1);
        const bool clear = std::none_of(agents_.begin(), agents_.end(), [&](const Agent& o) {
            return o.id != a.id && o.lane == lane && std::abs(o.s - newS) < 40.0;
        });
        if (clear) {
            a.s = newS;
            a.lane = lane;
            road_.ensureLength(newS + 100.0);
        }
    }
}

TelemetrySample DriveSimulator::sample() const {
    const KinematicState& ego = model_.state();
    TelemetrySample out;
    out.simTimeS = simTime_;
    out.x = ego.pose.position.x;
    out.y = ego.pose.position.y;
    const LatLon ll = enuToLatLon(config_.origin, ego.pose.position);
    out.latitude = ll.latitudeDeg;
    out.longitude = ll.longitudeDeg;
    out.heading = static_cast<float>(ego.pose.heading);
    out.speedMps = static_cast<float>(ego.speed);
    out.accelMps2 = static_cast<float>(ego.accel);
    out.yawRateRadS = static_cast<float>(ego.yawRate);
    out.steeringWheelDeg = static_cast<float>(ego.roadWheelAngle * model_.params().steeringRatio * kRadToDeg);
    out.throttle = static_cast<float>(lastActuation_.throttle);
    out.brake = static_cast<float>(lastActuation_.brake);
    out.setSpeedMps = static_cast<float>(cruiseSpeed_);
    const int lane = egoLane();
    out.lane = static_cast<std::int8_t>(lane);
    out.laneOffsetM = static_cast<float>(egoProjection_.lateral - road_.laneCenterOffset(lane));
    out.roadS = static_cast<float>(egoProjection_.s);
    out.gear = gear_;
    out.turnSignal = signal_;
    out.autopilot = autopilot();

    // Report the nearest agents, closest first, as a perception stack would.
    std::array<const Agent*, 32> nearby{};
    std::size_t n = 0;
    for (const Agent& a : agents_) {
        if (std::abs(a.s - egoProjection_.s) < kObjectRangeM && n < nearby.size()) {
            nearby[n++] = &a;
        }
    }
    const auto byDistance = [&](const Agent* l, const Agent* r) {
        return std::abs(l->s - egoProjection_.s) < std::abs(r->s - egoProjection_.s);
    };
    const std::size_t count = std::min(n, kMaxTrackedObjects);
    std::partial_sort(nearby.begin(), nearby.begin() + static_cast<std::ptrdiff_t>(count),
                      nearby.begin() + static_cast<std::ptrdiff_t>(n), byDistance);
    for (std::size_t i = 0; i < count; ++i) {
        const Agent& a = *nearby[i];
        const RoadPoint rp = road_.at(a.s);
        const Vec2 p = road_.pointAt(a.s, road_.laneCenterOffset(a.lane));
        out.objects[i] = TrackedObject{a.id,
                                       p.x,
                                       p.y,
                                       static_cast<float>(rp.heading),
                                       static_cast<float>(a.speed),
                                       static_cast<float>(kAgentLength),
                                       1.9F};
    }
    out.objectCount = static_cast<std::uint8_t>(count);
    return out;
}

} // namespace rvn
