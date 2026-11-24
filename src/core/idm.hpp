#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace rvn {

/// Intelligent Driver Model (Treiber et al., 2000): a smooth, collision-free car-following law.
/// Used for both background traffic and the ego vehicle's adaptive cruise control.
struct IdmParams {
    double maxAccel{1.6};     ///< a, m/s^2
    double comfortDecel{2.5}; ///< b, m/s^2
    double minGapM{4.0};      ///< s0
    double timeHeadwayS{1.6}; ///< T
    double exponent{4.0};     ///< delta
};

struct LeadVehicle {
    double gapM{}; ///< bumper-to-bumper
    double speedMps{};
};

inline double idmAcceleration(const IdmParams& p, double speed, double desiredSpeed,
                              std::optional<LeadVehicle> lead) noexcept {
    const double v = std::max(speed, 0.0);
    const double v0 = std::max(desiredSpeed, 0.1);
    double accel = p.maxAccel * (1.0 - std::pow(v / v0, p.exponent));
    if (lead) {
        const double dv = v - lead->speedMps;
        const double desiredGap =
            p.minGapM + std::max(0.0, v * p.timeHeadwayS + v * dv / (2.0 * std::sqrt(p.maxAccel * p.comfortDecel)));
        const double gap = std::max(lead->gapM, 0.1);
        accel -= p.maxAccel * (desiredGap / gap) * (desiredGap / gap);
    }
    return accel;
}

} // namespace rvn
