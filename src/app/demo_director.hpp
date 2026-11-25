#pragma once

#include <QObject>
#include <QTimer>

#include <cstddef>

namespace rvn {

class VehicleViewModel;

/// Plays a deterministic 30 s demonstration sequence through VehicleViewModel: Autopilot in
/// traffic, a left lane change, manual braking/throttle takeover, 4 kHz telemetry coalescing,
/// and a 3 s producer stall.
class DemoDirector : public QObject {
    Q_OBJECT
public:
    DemoDirector(VehicleViewModel& vehicle, int initialRateHz, bool loop = true, QObject* parent = nullptr);

    void start();
    void stop();

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] static constexpr int cycleDurationMs() noexcept { return 30000; }

signals:
    void cycleCompleted();

private:
    void scheduleNext();
    void runStep();

    VehicleViewModel& vehicle_;
    int initialRateHz_;
    bool loop_;
    bool running_{false};
    std::size_t stepIndex_{0};
    int elapsedMs_{0};
    QTimer timer_;
};

} // namespace rvn
