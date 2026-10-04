#include <gtest/gtest.h>

#include <cstdint>
#include <thread>
#include <vector>

#include "event_queue.hpp"
#include "event_generator.hpp"
#include "pipeline.hpp"

using namespace limitbook;

template <typename Q>
class QueueTest : public ::testing::Test {};
using QueueTypes = ::testing::Types<MutexQueue<std::uint64_t>, SpscRingBuffer<std::uint64_t>>;
TYPED_TEST_SUITE(QueueTest, QueueTypes);

TYPED_TEST(QueueTest, DeliversEverythingInOrderAcrossThreads) {
    constexpr std::uint64_t kCount = 200'000;
    TypeParam q(1'024);  // small capacity forces the full/empty paths
    std::vector<std::uint64_t> got;
    got.reserve(kCount);
    std::thread consumer([&] {
        std::uint64_t v;
        while (q.pop(v)) got.push_back(v);
    });
    for (std::uint64_t i = 0; i < kCount; ++i) q.push(i);
    q.close();
    consumer.join();
    ASSERT_EQ(got.size(), kCount);
    for (std::uint64_t i = 0; i < kCount; ++i) ASSERT_EQ(got[i], i);
}

TYPED_TEST(QueueTest, CloseOnEmptyQueueEndsConsumer) {
    TypeParam q(16);
    q.close();
    std::uint64_t v;
    EXPECT_FALSE(q.pop(v));
}

TYPED_TEST(QueueTest, ItemsPushedBeforeCloseAreDrained) {
    TypeParam q(16);
    q.push(1);
    q.push(2);
    q.close();
    std::uint64_t v;
    ASSERT_TRUE(q.pop(v));
    EXPECT_EQ(v, 1u);
    ASSERT_TRUE(q.pop(v));
    EXPECT_EQ(v, 2u);
    EXPECT_FALSE(q.pop(v));
}

// The threaded pipeline must produce exactly the same result as single-threaded replay.
TEST(PipelineTest, ConcurrentRunsMatchSingleThreaded) {
    EventGenerator gen;
    const auto events = gen.generate(100'000);

    BaselineEngine single, viaMutex, viaSpsc;
    const auto r0 = runSingleThreaded(single, events);
    const auto r1 = runPipeline<MutexQueue<TimedEvent>>(viaMutex, events, 4'096);
    const auto r2 = runPipeline<SpscRingBuffer<TimedEvent>>(viaSpsc, events, 4'096);

    EXPECT_EQ(r0.trades, r1.trades);
    EXPECT_EQ(r0.trades, r2.trades);
    EXPECT_EQ(r0.volume, r1.volume);
    EXPECT_EQ(r0.volume, r2.volume);
    EXPECT_EQ(single.book().orderCount(), viaMutex.book().orderCount());
    EXPECT_EQ(single.book().orderCount(), viaSpsc.book().orderCount());
    EXPECT_EQ(r1.latency.count, events.size());
}
