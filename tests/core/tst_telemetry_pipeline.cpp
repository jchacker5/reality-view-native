// End-to-end tests of the producer thread + lock-free queue + consumer, including the failure
// modes the UI must survive: a stalled producer, dropped samples, and a consumer that falls behind.
#include "drive_simulator.hpp"
#include "telemetry_consumer.hpp"
#include "telemetry_worker.hpp"

#include <QtTest>

#include <chrono>
#include <thread>

using namespace rvn;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {

std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

/// Drain at ~60 Hz for `duration`, like the UI would.
void pumpFrames(TelemetryWorker& w, TelemetryConsumer& c, std::chrono::milliseconds duration) {
    const auto end = Clock::now() + duration;
    while (Clock::now() < end) {
        c.drain(w.samples());
        std::this_thread::sleep_for(16ms);
    }
}

} // namespace

class TstTelemetryPipeline : public QObject {
    Q_OBJECT

private slots:
    void deliversSamplesInOrderWithoutGaps() {
        TelemetryWorker worker(std::make_unique<DriveSimulator>(), 500);
        TelemetryConsumer consumer;
        worker.start();
        pumpFrames(worker, consumer, 400ms);
        worker.stop();
        consumer.drain(worker.samples());

        QVERIFY(consumer.latest().has_value());
        QVERIFY2(consumer.totalConsumed() > 100, qPrintable(QString::number(consumer.totalConsumed())));
        QCOMPARE(consumer.totalGaps(), std::uint64_t{0});
        QCOMPARE(worker.overflowCount(), std::uint64_t{0});
        QCOMPARE(consumer.totalConsumed(), worker.producedCount());
        QVERIFY(!consumer.trail().empty());
        // Several samples per 16 ms frame at 500 Hz: the consumer coalesces them.
        QVERIFY(consumer.maxBatch() >= 4);
    }

    void stopIsPromptEvenDuringStall() {
        TelemetryWorker worker(std::make_unique<DriveSimulator>(), 240);
        worker.start();
        QVERIFY(worker.post(cmd::InjectStall{10'000}));
        QTRY_VERIFY_WITH_TIMEOUT(worker.stalled(), 1000);
        const auto t0 = Clock::now();
        worker.stop();
        QVERIFY2(Clock::now() - t0 < 500ms, "stop() must interrupt the stall, not wait 10 s");
    }

    void stallMakesDataStaleButConsumerNeverBlocks() {
        TelemetryWorker worker(std::make_unique<DriveSimulator>(), 240);
        TelemetryConsumer consumer;
        worker.start();
        pumpFrames(worker, consumer, 150ms);
        QVERIFY(consumer.ageSeconds(nowNs()) < 0.1);

        QVERIFY(worker.post(cmd::InjectStall{600}));
        QTRY_VERIFY_WITH_TIMEOUT(worker.stalled(), 1000);
        // Each drain must return immediately even though the producer is blocked.
        double worstDrainMs = 0.0;
        for (int i = 0; i < 20; ++i) {
            const auto t0 = Clock::now();
            consumer.drain(worker.samples());
            worstDrainMs = std::max(worstDrainMs, std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
            std::this_thread::sleep_for(16ms);
        }
        QVERIFY2(worstDrainMs < 5.0, qPrintable(QString("worst drain %1 ms").arg(worstDrainMs)));
        QVERIFY(consumer.ageSeconds(nowNs()) > 0.25);

        // After the stall the producer resynchronizes instead of bursting.
        QTRY_VERIFY_WITH_TIMEOUT(!worker.stalled(), 2000);
        pumpFrames(worker, consumer, 150ms);
        QVERIFY(consumer.ageSeconds(nowNs()) < 0.1);
        worker.stop();
    }

    void dropoutIsDetectedAsSequenceGaps() {
        TelemetryWorker worker(std::make_unique<DriveSimulator>(), 1000);
        TelemetryConsumer consumer;
        QVERIFY(worker.post(cmd::SetDropout{true}));
        worker.start();
        pumpFrames(worker, consumer, 300ms);
        worker.stop();
        consumer.drain(worker.samples());
        const double ratio = static_cast<double>(consumer.totalGaps()) /
                             static_cast<double>(consumer.totalGaps() + consumer.totalConsumed());
        QVERIFY2(ratio > 0.2 && ratio < 0.4, qPrintable(QString("gap ratio %1").arg(ratio)));
    }

    void slowConsumerOverflowsWithoutCorruption() {
        TelemetryWorker worker(std::make_unique<DriveSimulator>(), 4000);
        TelemetryConsumer consumer;
        worker.start();
        // Don't consume for long enough to overflow a 4096-slot queue at 4 kHz.
        QTRY_VERIFY_WITH_TIMEOUT(worker.overflowCount() > 0, 5000);
        pumpFrames(worker, consumer, 100ms);
        worker.stop();
        consumer.drain(worker.samples());
        // Overflowed samples were never published, so seq numbers jump: the consumer sees gaps.
        QVERIFY(consumer.totalGaps() > 0);
        QCOMPARE(consumer.totalConsumed(), worker.producedCount());
    }

    void publishRateCommandIsClamped() {
        TelemetryWorker worker(std::make_unique<DriveSimulator>(), 1);
        QCOMPARE(worker.publishRateHz(), TelemetryWorker::kMinRateHz);
        worker.start();
        QVERIFY(worker.post(cmd::SetPublishRate{1'000'000}));
        QTRY_COMPARE_WITH_TIMEOUT(worker.publishRateHz(), TelemetryWorker::kMaxRateHz, 1000);
        worker.stop();
    }

    void restartIsSafe() {
        TelemetryWorker worker(std::make_unique<DriveSimulator>(), 240);
        for (int i = 0; i < 5; ++i) {
            worker.start();
            std::this_thread::sleep_for(5ms);
            worker.stop();
        }
        QVERIFY(!worker.running());
    }
};

QTEST_GUILESS_MAIN(TstTelemetryPipeline)
#include "tst_telemetry_pipeline.moc"
