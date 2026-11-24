#pragma once

#include "telemetry_consumer.hpp"
#include "telemetry_worker.hpp"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace rvn {

/// GUI-thread facade over the telemetry pipeline, exposed to QML.
///
/// Update model: QML calls tick() once per frame (FrameAnimation). tick() drains the worker's
/// queue in one batch and emits ONE telemetryChanged signal, so every binding on every
/// property re-evaluates at most once per frame no matter how fast telemetry arrives. Slower-
/// moving data (pipeline health, chart series) is published on its own throttled signals.
class VehicleViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created in C++ and injected as a required property")

    // ---- Per-frame telemetry (NOTIFY telemetryChanged) -----------------------------------
    Q_PROPERTY(bool hasData READ hasData NOTIFY telemetryChanged)
    Q_PROPERTY(double speedMph READ speedMph NOTIFY telemetryChanged)
    Q_PROPERTY(double accelMps2 READ accelMps2 NOTIFY telemetryChanged)
    Q_PROPERTY(QString gear READ gear NOTIFY telemetryChanged)
    Q_PROPERTY(double steeringWheelDeg READ steeringWheelDeg NOTIFY telemetryChanged)
    Q_PROPERTY(double throttle READ throttle NOTIFY telemetryChanged)
    Q_PROPERTY(double brake READ brake NOTIFY telemetryChanged)
    Q_PROPERTY(int turnSignal READ turnSignal NOTIFY telemetryChanged)
    Q_PROPERTY(int autopilotState READ autopilotState NOTIFY telemetryChanged)
    Q_PROPERTY(QString autopilotText READ autopilotText NOTIFY telemetryChanged)
    Q_PROPERTY(double setSpeedMph READ setSpeedMph NOTIFY telemetryChanged)
    Q_PROPERTY(int lane READ lane NOTIFY telemetryChanged)
    Q_PROPERTY(double laneOffsetM READ laneOffsetM NOTIFY telemetryChanged)
    Q_PROPERTY(double latitude READ latitude NOTIFY telemetryChanged)
    Q_PROPERTY(double longitude READ longitude NOTIFY telemetryChanged)
    Q_PROPERTY(double headingDeg READ headingDeg NOTIFY telemetryChanged)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY telemetryChanged)
    Q_PROPERTY(double simTime READ simTime NOTIFY telemetryChanged)
    Q_PROPERTY(double odometerKm READ odometerKm NOTIFY telemetryChanged)

    // ---- Pipeline health (NOTIFY pipelineChanged, 4 Hz) ----------------------------------
    Q_PROPERTY(int publishRateHz READ publishRateHz NOTIFY pipelineChanged)
    Q_PROPERTY(double measuredRateHz READ measuredRateHz NOTIFY pipelineChanged)
    Q_PROPERTY(double samplesPerFrame READ samplesPerFrame NOTIFY pipelineChanged)
    Q_PROPERTY(int maxBatch READ maxBatch NOTIFY pipelineChanged)
    Q_PROPERTY(double totalGaps READ totalGaps NOTIFY pipelineChanged)
    Q_PROPERTY(double overflowCount READ overflowCount NOTIFY pipelineChanged)
    Q_PROPERTY(double dataAgeMs READ dataAgeMs NOTIFY pipelineChanged)
    Q_PROPERTY(bool stale READ stale NOTIFY pipelineChanged)
    Q_PROPERTY(bool producerStalled READ producerStalled NOTIFY pipelineChanged)
    Q_PROPERTY(bool dropoutEnabled READ dropoutEnabled NOTIFY pipelineChanged)
    Q_PROPERTY(double drainMicros READ drainMicros NOTIFY pipelineChanged)
    Q_PROPERTY(int queueDepth READ queueDepth NOTIFY pipelineChanged)

    // ---- Chart series (NOTIFY seriesChanged, 10 Hz). QList is implicitly shared: handing it
    //      to QML copies a pointer, not the data.
    Q_PROPERTY(QList<float> speedSeries READ speedSeries NOTIFY seriesChanged)

    Q_PROPERTY(int roadSeed READ roadSeed CONSTANT)
    Q_PROPERTY(bool replay READ replay CONSTANT)

public:
    VehicleViewModel(TelemetryWorker& worker, bool replay, QObject* parent = nullptr);

    /// Drain the pipeline. Call once per frame on the GUI thread. Returns samples consumed.
    Q_INVOKABLE int tick();

    Q_INVOKABLE void setManualInput(double throttle, double brake, double steer);
    Q_INVOKABLE void shiftGear(const QString& gear);
    Q_INVOKABLE void signalTurn(int signal); ///< 0 off, 1 left, 2 right
    Q_INVOKABLE void toggleAutopilot();
    Q_INVOKABLE void adjustCruise(double deltaMph);
    Q_INVOKABLE void setPublishRate(int hz);
    Q_INVOKABLE void injectStall(int milliseconds);
    Q_INVOKABLE void setDropout(bool enabled);
    Q_INVOKABLE void reset();

    // Accessors for C++ consumers (the scene item reads these during its GUI-thread snapshot).
    [[nodiscard]] const TelemetryConsumer& consumer() const noexcept { return consumer_; }

    [[nodiscard]] bool hasData() const { return consumer_.latest().has_value(); }
    [[nodiscard]] double speedMph() const;
    [[nodiscard]] double accelMps2() const;
    [[nodiscard]] QString gear() const;
    [[nodiscard]] double steeringWheelDeg() const;
    [[nodiscard]] double throttle() const;
    [[nodiscard]] double brake() const;
    [[nodiscard]] int turnSignal() const;
    [[nodiscard]] int autopilotState() const;
    [[nodiscard]] QString autopilotText() const;
    [[nodiscard]] double setSpeedMph() const;
    [[nodiscard]] int lane() const;
    [[nodiscard]] double laneOffsetM() const;
    [[nodiscard]] double latitude() const;
    [[nodiscard]] double longitude() const;
    [[nodiscard]] double headingDeg() const;
    [[nodiscard]] int objectCount() const;
    [[nodiscard]] double simTime() const;
    [[nodiscard]] double odometerKm() const { return odometerM_ / 1000.0; }

    [[nodiscard]] int publishRateHz() const { return worker_.publishRateHz(); }
    [[nodiscard]] double measuredRateHz() const { return measuredRateHz_; }
    [[nodiscard]] double samplesPerFrame() const { return samplesPerFrame_; }
    [[nodiscard]] int maxBatch() const { return static_cast<int>(consumer_.maxBatch()); }
    [[nodiscard]] double totalGaps() const { return static_cast<double>(consumer_.totalGaps()); }
    [[nodiscard]] double overflowCount() const { return static_cast<double>(worker_.overflowCount()); }
    [[nodiscard]] double dataAgeMs() const { return dataAgeMs_; }
    [[nodiscard]] bool stale() const { return dataAgeMs_ > kStaleMs; }
    [[nodiscard]] bool producerStalled() const { return worker_.stalled(); }
    [[nodiscard]] bool dropoutEnabled() const { return dropout_; }
    [[nodiscard]] double drainMicros() const { return drainMicros_; }
    [[nodiscard]] int queueDepth() const { return static_cast<int>(worker_.samples().sizeApprox()); }
    [[nodiscard]] QList<float> speedSeries() const { return speedSeries_; }
    [[nodiscard]] int roadSeed() const { return static_cast<int>(worker_.roadSeed()); }
    [[nodiscard]] bool replay() const { return replay_; }

    static constexpr double kStaleMs = 250.0;

signals:
    void telemetryChanged();
    void pipelineChanged();
    void seriesChanged();

private:
    void post(const Command& c);
    void publishPipelineStats();

    TelemetryWorker& worker_;
    TelemetryConsumer consumer_;
    bool replay_;
    bool dropout_{false};

    QElapsedTimer pipelineClock_;
    QElapsedTimer seriesClock_;
    std::uint64_t consumedAtLastPublish_{0};
    std::uint64_t framesSincePublish_{0};
    double measuredRateHz_{0.0};
    double samplesPerFrame_{0.0};
    double dataAgeMs_{0.0};
    double drainMicros_{0.0};
    double drainMicrosAccum_{0.0};
    double odometerM_{0.0};
    std::optional<Vec2> lastPosition_;
    QList<float> speedSeries_;
};

} // namespace rvn
