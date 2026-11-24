#include "spsc_ring_buffer.hpp"

#include <QtTest>

#include <cstdint>
#include <thread>

using rvn::SpscRingBuffer;

class TstSpscRingBuffer : public QObject {
    Q_OBJECT

private slots:
    void startsEmpty() {
        SpscRingBuffer<int, 8> q;
        int out = 0;
        QVERIFY(!q.tryPop(out));
        QCOMPARE(q.sizeApprox(), std::size_t{0});
        QCOMPARE(q.capacity(), std::size_t{8});
    }

    void fifoOrderAndFull() {
        SpscRingBuffer<int, 4> q;
        for (int i = 0; i < 4; ++i) {
            QVERIFY(q.tryPush(i));
        }
        QVERIFY2(!q.tryPush(99), "push into a full queue must fail, not overwrite");
        int out = -1;
        for (int i = 0; i < 4; ++i) {
            QVERIFY(q.tryPop(out));
            QCOMPARE(out, i);
        }
        QVERIFY(!q.tryPop(out));
    }

    void wrapsAroundManyTimes() {
        SpscRingBuffer<int, 4> q;
        int out = 0;
        for (int i = 0; i < 10'000; ++i) {
            QVERIFY(q.tryPush(i));
            QVERIFY(q.tryPush(i + 1));
            QVERIFY(q.tryPop(out));
            QCOMPARE(out, i);
            QVERIFY(q.tryPop(out));
            QCOMPARE(out, i + 1);
        }
    }

    void drainRespectsLimitAndOrder() {
        SpscRingBuffer<int, 16> q;
        for (int i = 0; i < 10; ++i) {
            QVERIFY(q.tryPush(i));
        }
        std::vector<int> seen;
        QCOMPARE(q.drain([&](int v) { seen.push_back(v); }, 4), std::size_t{4});
        QCOMPARE(q.drain([&](int v) { seen.push_back(v); }), std::size_t{6});
        QCOMPARE(q.drain([&](int v) { seen.push_back(v); }), std::size_t{0});
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(seen[static_cast<std::size_t>(i)], i);
        }
    }

    // The real test of a lock-free queue: two threads, millions of items, every item arrives
    // exactly once and in order. Run under ThreadSanitizer in CI (see .github/workflows/ci.yml).
    void concurrentProducerConsumerPreservesSequence() {
        constexpr std::uint64_t kItems = 2'000'000;
        auto q = std::make_unique<SpscRingBuffer<std::uint64_t, 1024>>();

        std::thread producer([&] {
            for (std::uint64_t i = 0; i < kItems;) {
                if (q->tryPush(i)) {
                    ++i;
                }
            }
        });

        std::uint64_t expected = 0;
        bool ordered = true;
        while (expected < kItems) {
            q->drain([&](std::uint64_t v) {
                ordered = ordered && (v == expected);
                ++expected;
            });
        }
        producer.join();
        QVERIFY(ordered);
        QCOMPARE(expected, kItems);
    }

    void countersSurviveUnsignedWrap() {
        // Free-running counters rely on unsigned wrap-around; exercise the arithmetic directly.
        const std::size_t head = 1;
        const std::size_t tail = std::numeric_limits<std::size_t>::max(); // head wrapped, tail not yet
        QCOMPARE(head - tail, std::size_t{2});
    }
};

QTEST_GUILESS_MAIN(TstSpscRingBuffer)
#include "tst_spsc_ring_buffer.moc"
