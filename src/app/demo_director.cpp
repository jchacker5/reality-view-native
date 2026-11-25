#include "demo_director.hpp"

#include "vehicle_view_model.hpp"

#include <algorithm>
#include <array>

namespace rvn {
namespace {

struct DemoStep {
    int atMs;
    void (*run)(VehicleViewModel& vehicle);
};

constexpr std::array<DemoStep, 7> kSteps{{
    // 1. Autopilot cruises in traffic for the first 5.5 s, then signals a left lane change (Q).
    {5500, [](VehicleViewModel& v) { v.signalTurn(1); }},
    // 2. Manual takeover by firm braking (S): disengages Autopilot to Standby while keeping
    //    the car inside the lane before re-engaging.
    {10500, [](VehicleViewModel& v) { v.setManualInput(0.0, 1.0, -0.12); }},
    // 3. Release brake and re-engage Autopilot (Space) so the car stays centered on the curve.
    {11800,
     [](VehicleViewModel& v) {
         v.setManualInput(0.0, 0.0, 0.0);
         if (v.autopilotState() != 2) {
             v.toggleAutopilot();
         }
     }},
    // Restore 65 mph cruise once tick() has drained the post-engage set speed.
    {12150, [](VehicleViewModel& v) { v.adjustCruise(65.0 - v.setSpeedMph()); }},
    // 4. Switch telemetry publish rate 16x up to 4 kHz with the frame rate unchanged.
    {14500, [](VehicleViewModel& v) { v.setPublishRate(4000); }},
    // 5. Stall the producer for 3 s: the "Telemetry stale" banner pulses while the UI stays smooth.
    {20500, [](VehicleViewModel& v) { v.injectStall(3000); }},
    // 6. End of cycle after recovery; reset when looping.
    {DemoDirector::cycleDurationMs(), [](VehicleViewModel&) {}},
}};

} // namespace

DemoDirector::DemoDirector(VehicleViewModel& vehicle, int initialRateHz, bool loop, QObject* parent)
    : QObject(parent), vehicle_(vehicle), initialRateHz_(initialRateHz), loop_(loop) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &DemoDirector::runStep);
}

void DemoDirector::start() {
    if (running_) {
        return;
    }
    running_ = true;
    stepIndex_ = 0;
    elapsedMs_ = 0;
    scheduleNext();
}

void DemoDirector::stop() {
    running_ = false;
    timer_.stop();
}

void DemoDirector::scheduleNext() {
    if (!running_ || stepIndex_ >= kSteps.size()) {
        return;
    }
    const int delayMs = std::max(0, kSteps[stepIndex_].atMs - elapsedMs_);
    timer_.start(delayMs);
}

void DemoDirector::runStep() {
    if (!running_ || stepIndex_ >= kSteps.size()) {
        return;
    }
    const DemoStep& step = kSteps[stepIndex_];
    elapsedMs_ = step.atMs;
    step.run(vehicle_);
    ++stepIndex_;

    if (stepIndex_ >= kSteps.size()) {
        emit cycleCompleted();
        if (loop_) {
            vehicle_.setManualInput(0.0, 0.0, 0.0);
            vehicle_.setPublishRate(initialRateHz_);
            vehicle_.reset();
            stepIndex_ = 0;
            elapsedMs_ = 0;
            scheduleNext();
        } else {
            running_ = false;
        }
        return;
    }
    scheduleNext();
}

} // namespace rvn
