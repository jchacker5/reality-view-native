#include "can_source.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace rvn {
namespace {

struct SignalMapping {
    std::string_view name;
    // Matches CanTelemetryCodec field order via integer tag in cpp.
    int kind;
};

constexpr SignalMapping kSignalMap[] = {
    {"VehicleSpeed", 0},    {"LongitudinalAccel", 1},  {"YawRate", 2},         {"GearState", 3},
    {"RoadSeed", 4},        {"SteeringWheelAngle", 5}, {"ThrottlePos", 6},     {"BrakePos", 7},
    {"TurnSignalState", 8}, {"AutopilotState", 9},     {"CruiseSetSpeed", 10}, {"LaneIndex", 11},
    {"LaneOffset", 12},     {"RoadArcLength", 13},     {"WorldPosX", 14},      {"WorldPosY", 15},
    {"VehicleHeading", 16},
};

int findFieldKind(std::string_view name) noexcept {
    for (const auto& m : kSignalMap) {
        if (m.name == name) {
            return m.kind;
        }
    }
    return -1;
}

Gear gearFromInt(int v) noexcept {
    switch (v) {
    case 0: return Gear::Park;
    case 1: return Gear::Reverse;
    case 2: return Gear::Neutral;
    case 3: return Gear::Drive;
    default: return Gear::Park;
    }
}

TurnSignal signalFromInt(int v) noexcept {
    switch (v) {
    case 1: return TurnSignal::Left;
    case 2: return TurnSignal::Right;
    default: return TurnSignal::Off;
    }
}

AutopilotState autopilotFromInt(int v) noexcept {
    switch (v) {
    case 1: return AutopilotState::Standby;
    case 2: return AutopilotState::Active;
    default: return AutopilotState::Unavailable;
    }
}

} // namespace

CanTelemetryCodec::CanTelemetryCodec(DbcDatabase db) : db_(std::move(db)) {
    for (const DbcMessage& msg : db_.messages()) {
        BoundMessage bm;
        bm.id = msg.id;
        bm.extended = msg.extended;
        bm.dlc = msg.dlc;
        for (const DbcSignal& sig : msg.signalDefs) {
            const int kind = findFieldKind(sig.name);
            if (kind >= 0) {
                bm.fields.push_back(BoundSignal{static_cast<Field>(kind), sig});
            }
        }
        if (!bm.fields.empty()) {
            messages_.push_back(std::move(bm));
        }
    }
}

std::vector<CanFrame> CanTelemetryCodec::encode(const TelemetrySample& sample, std::uint64_t roadSeed) const {
    std::vector<CanFrame> frames;
    frames.reserve(messages_.size());
    for (const BoundMessage& bm : messages_) {
        CanFrame frame;
        frame.id = bm.id;
        frame.extended = bm.extended;
        frame.dlc = bm.dlc;
        for (const BoundSignal& bs : bm.fields) {
            double physical = 0.0;
            switch (bs.field) {
            case Field::VehicleSpeed: physical = static_cast<double>(sample.speedMps); break;
            case Field::LongitudinalAccel: physical = static_cast<double>(sample.accelMps2); break;
            case Field::YawRate: physical = static_cast<double>(sample.yawRateRadS); break;
            case Field::GearState: physical = static_cast<double>(static_cast<int>(sample.gear)); break;
            case Field::RoadSeed: physical = static_cast<double>(roadSeed); break;
            case Field::SteeringWheelAngle: physical = static_cast<double>(sample.steeringWheelDeg); break;
            case Field::ThrottlePos: physical = static_cast<double>(sample.throttle); break;
            case Field::BrakePos: physical = static_cast<double>(sample.brake); break;
            case Field::TurnSignalState: physical = static_cast<double>(static_cast<int>(sample.turnSignal)); break;
            case Field::AutopilotState: physical = static_cast<double>(static_cast<int>(sample.autopilot)); break;
            case Field::CruiseSetSpeed: physical = static_cast<double>(sample.setSpeedMps); break;
            case Field::LaneIndex: physical = static_cast<double>(sample.lane); break;
            case Field::LaneOffset: physical = static_cast<double>(sample.laneOffsetM); break;
            case Field::RoadArcLength: physical = static_cast<double>(sample.roadS); break;
            case Field::WorldPosX: physical = sample.x; break;
            case Field::WorldPosY: physical = sample.y; break;
            case Field::VehicleHeading: physical = wrapAngle(static_cast<double>(sample.heading)); break;
            }
            bs.signal.encode(physical, frame.data);
        }
        frames.push_back(frame);
    }
    return frames;
}

bool CanTelemetryCodec::decodeInto(const CanFrame& frame, TelemetrySample& sample, std::uint64_t& roadSeed,
                                   const GeoOrigin& origin) const noexcept {
    const BoundMessage* match = nullptr;
    for (const BoundMessage& bm : messages_) {
        if (bm.id == frame.id && bm.extended == frame.extended) {
            match = &bm;
            break;
        }
    }
    if (match == nullptr) {
        return false;
    }
    bool poseChanged = false;
    for (const BoundSignal& bs : match->fields) {
        const double v = bs.signal.decode(frame.data);
        switch (bs.field) {
        case Field::VehicleSpeed: sample.speedMps = static_cast<float>(v); break;
        case Field::LongitudinalAccel: sample.accelMps2 = static_cast<float>(v); break;
        case Field::YawRate: sample.yawRateRadS = static_cast<float>(v); break;
        case Field::GearState: sample.gear = gearFromInt(static_cast<int>(std::llround(v))); break;
        case Field::RoadSeed: roadSeed = v >= 0.0 ? static_cast<std::uint64_t>(std::llround(v)) : 0ULL; break;
        case Field::SteeringWheelAngle: sample.steeringWheelDeg = static_cast<float>(v); break;
        case Field::ThrottlePos: sample.throttle = static_cast<float>(std::clamp(v, 0.0, 1.0)); break;
        case Field::BrakePos: sample.brake = static_cast<float>(std::clamp(v, 0.0, 1.0)); break;
        case Field::TurnSignalState: sample.turnSignal = signalFromInt(static_cast<int>(std::llround(v))); break;
        case Field::AutopilotState: sample.autopilot = autopilotFromInt(static_cast<int>(std::llround(v))); break;
        case Field::CruiseSetSpeed: sample.setSpeedMps = static_cast<float>(v); break;
        case Field::LaneIndex:
            sample.lane = static_cast<std::int8_t>(std::clamp<long long>(std::llround(v), 0, 15));
            break;
        case Field::LaneOffset: sample.laneOffsetM = static_cast<float>(v); break;
        case Field::RoadArcLength: sample.roadS = static_cast<float>(v); break;
        case Field::WorldPosX:
            sample.x = v;
            poseChanged = true;
            break;
        case Field::WorldPosY:
            sample.y = v;
            poseChanged = true;
            break;
        case Field::VehicleHeading: sample.heading = static_cast<float>(v); break;
        }
    }
    if (poseChanged) {
        const LatLon ll = enuToLatLon(origin, Vec2{sample.x, sample.y});
        sample.latitude = ll.latitudeDeg;
        sample.longitude = ll.longitudeDeg;
    }
    return true;
}

CanTelemetrySource::CanTelemetrySource(DbcDatabase db, std::unique_ptr<CanBus> bus, std::uint64_t defaultSeed,
                                       GeoOrigin origin)
    : codec_(std::move(db)), bus_(std::move(bus)), defaultSeed_(defaultSeed), roadSeed_(defaultSeed), origin_(origin) {
    if (!bus_) {
        throw std::invalid_argument("CanTelemetrySource requires a non-null CanBus");
    }
}

CanTelemetrySource::CanTelemetrySource(DbcDatabase db, std::vector<CandumpEntry> entries, std::uint64_t defaultSeed,
                                       GeoOrigin origin)
    : codec_(std::move(db)), log_(std::move(entries)), defaultSeed_(defaultSeed), roadSeed_(defaultSeed),
      origin_(origin) {
    if (log_.empty()) {
        throw std::invalid_argument("candump log has no CAN frames");
    }
    logDuration_ = log_.back().timestampS;
    // Decode any initial frames at t = 0 so the very first sample is populated.
    while (logCursor_ < log_.size() && log_[logCursor_].timestampS <= 0.0) {
        codec_.decodeInto(log_[logCursor_].frame, current_, roadSeed_, origin_);
        ++logCursor_;
    }
}

void CanTelemetrySource::apply(const Command& command) {
    if (std::holds_alternative<cmd::Reset>(command)) {
        logCursor_ = 0;
        playhead_ = 0.0;
        simTime_ = 0.0;
        roadSeed_ = defaultSeed_;
        current_ = TelemetrySample{};
        while (logCursor_ < log_.size() && log_[logCursor_].timestampS <= 0.0) {
            codec_.decodeInto(log_[logCursor_].frame, current_, roadSeed_, origin_);
            ++logCursor_;
        }
    }
}

void CanTelemetrySource::step(double dt) {
    if (dt <= 0.0) {
        return;
    }
    simTime_ += dt;

    if (bus_) {
        CanFrame frame;
        for (int i = 0; i < 256 && bus_->receive(frame, 0); ++i) {
            codec_.decodeInto(frame, current_, roadSeed_, origin_);
        }
        return;
    }

    if (logDuration_ > 0.0) {
        playhead_ += dt;
        if (playhead_ > logDuration_ + 1e-9) {
            playhead_ = std::fmod(playhead_, logDuration_);
            logCursor_ = 0;
        }
    }
    while (logCursor_ < log_.size() && log_[logCursor_].timestampS <= playhead_ + 1e-9) {
        codec_.decodeInto(log_[logCursor_].frame, current_, roadSeed_, origin_);
        ++logCursor_;
    }
}

TelemetrySample CanTelemetrySource::sample() const {
    TelemetrySample out = current_;
    out.simTimeS = simTime_;
    return out;
}

} // namespace rvn
