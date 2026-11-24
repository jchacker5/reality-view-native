#include "frame_stats.hpp"
#include "process_stats.hpp"
#include "ring_history.hpp"

#include <QtTest>

#include <vector>

using namespace rvn;

class TstFrameStats : public QObject {
    Q_OBJECT

private slots:
    void emptySummaryIsZero() {
        const FrameStats fs;
        const auto s = fs.summary();
        QCOMPARE(s.fps, 0.0);
        QCOMPARE(s.totalFrames, std::uint64_t{0});
    }

    void steady60Hz() {
        FrameStats fs(120);
        for (int i = 0; i < 120; ++i) {
            fs.addFrame(1000.0 / 60.0);
        }
        const auto s = fs.summary();
        QVERIFY(std::abs(s.fps - 60.0) < 1e-6);
        QVERIFY(std::abs(s.p99Ms - 16.6667) < 1e-3);
        QCOMPARE(s.windowJank, std::size_t{0});
    }

    void percentilesAndJank() {
        FrameStats fs(100);
        for (int i = 1; i <= 100; ++i) {
            fs.addFrame(static_cast<double>(i)); // 1..100 ms
        }
        const auto s = fs.summary();
        QCOMPARE(s.p50Ms, 50.0);
        QCOMPARE(s.p95Ms, 95.0);
        QCOMPARE(s.p99Ms, 99.0);
        QCOMPARE(s.maxMs, 100.0);
        // > 25 ms (1.5 x 16.67) is jank: frames 26..100.
        QCOMPARE(s.windowJank, std::size_t{75});
    }

    void windowSlides() {
        FrameStats fs(10);
        for (int i = 0; i < 10; ++i) {
            fs.addFrame(100.0);
        }
        for (int i = 0; i < 10; ++i) {
            fs.addFrame(10.0);
        }
        const auto s = fs.summary();
        QCOMPARE(s.maxMs, 10.0);
        QCOMPARE(s.windowJank, std::size_t{0});
        QCOMPARE(s.totalJank, std::uint64_t{10}); // lifetime count remembers
        QCOMPARE(s.totalFrames, std::uint64_t{20});
    }

    void ringHistoryOverwritesOldest() {
        RingHistory<int, 3> h;
        for (int i = 1; i <= 5; ++i) {
            h.push(i);
        }
        QCOMPARE(h.size(), std::size_t{3});
        QCOMPARE(h[0], 3);
        QCOMPARE(h.back(), 5);
        std::vector<int> out;
        h.copyTo(std::back_inserter(out));
        QCOMPARE(out, (std::vector<int>{3, 4, 5}));
    }

    void processUsageIsSane() {
        const ProcessUsage u = sampleProcessUsage();
        QVERIFY(u.residentBytes > 1024 * 1024);
        QVERIFY(u.cpuSeconds >= 0.0);
        CpuMeter meter;
        QCOMPARE(meter.update(), 0.0);
        volatile double sink = 0;
        for (int i = 0; i < 5'000'000; ++i) {
            sink = sink + 1.0;
        }
        QVERIFY(meter.update() >= 0.0);
    }
};

QTEST_GUILESS_MAIN(TstFrameStats)
#include "tst_frame_stats.moc"
