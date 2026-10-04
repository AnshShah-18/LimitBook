#include <gtest/gtest.h>

#include <vector>

#include "book_types.hpp"
#include "matching_engine.hpp"

using namespace aquila;

template <typename Book>
class MatchingEngineTest : public ::testing::Test {
protected:
    BasicMatchingEngine<Book> engine;
    std::vector<Trade> trades;

    Status buy(OrderId id, Price p, Quantity q) { return engine.add(id, Side::Buy, p, q, trades); }
    Status sell(OrderId id, Price p, Quantity q) { return engine.add(id, Side::Sell, p, q, trades); }
    Status modify(OrderId id, Price p, Quantity q) { return engine.modify(id, p, q, trades); }
    const Order* order(OrderId id) { return engine.book().find(id); }
};
TYPED_TEST_SUITE(MatchingEngineTest, BookTypes);

TYPED_TEST(MatchingEngineTest, NonCrossingOrdersRest) {
    EXPECT_EQ(this->buy(1, 9'900, 100), Status::Accepted);
    EXPECT_EQ(this->sell(2, 10'000, 100), Status::Accepted);
    EXPECT_TRUE(this->trades.empty());
    EXPECT_EQ(this->engine.book().bestBid(), 9'900);
    EXPECT_EQ(this->engine.book().bestAsk(), 10'000);
}

TYPED_TEST(MatchingEngineTest, SpecExamplePartialFillOfRestingOrder) {
    // SELL 100 @ 100.00; BUY 60 @ 101.00 -> 60 trade @ 100.00, 40 left on the ask.
    this->sell(1, 10'000, 100);
    this->buy(2, 10'100, 60);
    ASSERT_EQ(this->trades.size(), 1u);
    EXPECT_EQ(this->trades[0], (Trade{1, 2, 1, 10'000, 60, this->trades[0].seq}));
    EXPECT_EQ(this->order(1)->remaining, 40u);
    EXPECT_EQ(this->order(2), nullptr);  // fully filled, never rests
}

TYPED_TEST(MatchingEngineTest, SpecExampleSweepsMultipleLevels) {
    // SELL 100 @ 100, SELL 100 @ 101; BUY 150 @ 102 -> 100 @ 100, 50 @ 101.
    this->sell(1, 10'000, 100);
    this->sell(2, 10'100, 100);
    this->buy(3, 10'200, 150);
    ASSERT_EQ(this->trades.size(), 2u);
    EXPECT_EQ(this->trades[0].price, 10'000);
    EXPECT_EQ(this->trades[0].quantity, 100u);
    EXPECT_EQ(this->trades[1].price, 10'100);
    EXPECT_EQ(this->trades[1].quantity, 50u);
    EXPECT_EQ(this->order(1), nullptr);
    EXPECT_EQ(this->order(2)->remaining, 50u);
    EXPECT_EQ(this->engine.book().bestAsk(), 10'100);
    EXPECT_FALSE(this->engine.book().bestBid().has_value());
}

TYPED_TEST(MatchingEngineTest, FullFillBothSides) {
    this->buy(1, 10'000, 100);
    this->sell(2, 10'000, 100);
    ASSERT_EQ(this->trades.size(), 1u);
    EXPECT_EQ(this->trades[0].quantity, 100u);
    EXPECT_EQ(this->engine.book().orderCount(), 0u);
}

TYPED_TEST(MatchingEngineTest, IncomingRemainderRests) {
    this->sell(1, 10'000, 30);
    this->buy(2, 10'050, 100);
    ASSERT_EQ(this->trades.size(), 1u);
    ASSERT_NE(this->order(2), nullptr);
    EXPECT_EQ(this->order(2)->remaining, 70u);
    EXPECT_EQ(this->order(2)->filled(), 30u);
    EXPECT_EQ(this->engine.book().bestBid(), 10'050);
}

TYPED_TEST(MatchingEngineTest, TimePriorityWithinLevel) {
    this->buy(1, 10'125, 100);
    this->buy(2, 10'125, 200);
    this->buy(3, 10'120, 300);
    this->sell(4, 10'100, 250);
    ASSERT_EQ(this->trades.size(), 2u);
    EXPECT_EQ(this->trades[0].buyOrderId, 1u);  // oldest at best price first
    EXPECT_EQ(this->trades[0].quantity, 100u);
    EXPECT_EQ(this->trades[1].buyOrderId, 2u);
    EXPECT_EQ(this->trades[1].quantity, 150u);
    EXPECT_EQ(this->order(2)->remaining, 50u);
    EXPECT_EQ(this->order(3)->remaining, 300u);  // worse price untouched
}

TYPED_TEST(MatchingEngineTest, ExecutionAtMakerPrice) {
    this->buy(1, 10'000, 10);
    this->sell(2, 9'500, 10);  // aggressive seller still gets the resting bid's price
    ASSERT_EQ(this->trades.size(), 1u);
    EXPECT_EQ(this->trades[0].price, 10'000);
    EXPECT_EQ(this->trades[0].buyOrderId, 1u);
    EXPECT_EQ(this->trades[0].sellOrderId, 2u);
}

TYPED_TEST(MatchingEngineTest, LimitPriceStopsSweep) {
    this->sell(1, 10'000, 10);
    this->sell(2, 10'200, 10);
    this->buy(3, 10'100, 50);
    ASSERT_EQ(this->trades.size(), 1u);
    EXPECT_EQ(this->order(3)->remaining, 40u);
    EXPECT_EQ(this->engine.book().bestBid(), 10'100);
    EXPECT_EQ(this->engine.book().bestAsk(), 10'200);
}

TYPED_TEST(MatchingEngineTest, CancelledOrderCannotExecute) {
    this->sell(1, 10'000, 100);
    EXPECT_EQ(this->engine.cancel(1), Status::Cancelled);
    this->buy(2, 10'000, 100);
    EXPECT_TRUE(this->trades.empty());
    EXPECT_EQ(this->order(2)->remaining, 100u);
}

TYPED_TEST(MatchingEngineTest, CancelUnknownOrFilledIsNotFound) {
    EXPECT_EQ(this->engine.cancel(999), Status::NotFound);
    this->sell(1, 10'000, 10);
    this->buy(2, 10'000, 10);
    EXPECT_EQ(this->engine.cancel(1), Status::NotFound);
    EXPECT_EQ(this->engine.stats().notFound, 2u);
}

TYPED_TEST(MatchingEngineTest, ModifyReduceKeepsPriority) {
    this->buy(1, 10'000, 100);
    this->buy(2, 10'000, 100);
    EXPECT_EQ(this->modify(1, 10'000, 50), Status::Accepted);
    this->sell(3, 10'000, 60);
    ASSERT_EQ(this->trades.size(), 2u);
    EXPECT_EQ(this->trades[0].buyOrderId, 1u);
    EXPECT_EQ(this->trades[0].quantity, 50u);
    EXPECT_EQ(this->trades[1].buyOrderId, 2u);
    EXPECT_EQ(this->trades[1].quantity, 10u);
}

TYPED_TEST(MatchingEngineTest, ModifyIncreaseLosesPriority) {
    this->buy(1, 10'000, 100);
    this->buy(2, 10'000, 100);
    EXPECT_EQ(this->modify(1, 10'000, 150), Status::Accepted);
    this->sell(3, 10'000, 100);
    ASSERT_EQ(this->trades.size(), 1u);
    EXPECT_EQ(this->trades[0].buyOrderId, 2u);  // 1 went to the back of the queue
    EXPECT_EQ(this->order(1)->remaining, 150u);
}

TYPED_TEST(MatchingEngineTest, ModifyPriceMovesLevel) {
    this->buy(1, 10'000, 100);
    EXPECT_EQ(this->modify(1, 9'950, 100), Status::Accepted);
    EXPECT_EQ(this->engine.book().bestBid(), 9'950);
    EXPECT_EQ(this->engine.book().levelCount(Side::Buy), 1u);
}

TYPED_TEST(MatchingEngineTest, ModifyIntoCrossTrades) {
    this->sell(1, 10'100, 40);
    this->buy(2, 10'000, 100);
    EXPECT_EQ(this->modify(2, 10'100, 100), Status::Accepted);
    ASSERT_EQ(this->trades.size(), 1u);
    EXPECT_EQ(this->trades[0].quantity, 40u);
    EXPECT_EQ(this->trades[0].price, 10'100);
    EXPECT_EQ(this->order(2)->remaining, 60u);
}

TYPED_TEST(MatchingEngineTest, ModifyInvalidInputs) {
    EXPECT_EQ(this->modify(42, 10'000, 10), Status::NotFound);
    this->buy(1, 10'000, 10);
    EXPECT_EQ(this->modify(1, 10'000, 0), Status::RejectedInvalid);
    EXPECT_EQ(this->modify(1, 0, 10), Status::RejectedInvalid);
    EXPECT_EQ(this->order(1)->remaining, 10u);  // unchanged
}

TYPED_TEST(MatchingEngineTest, DuplicateOrderIdRejected) {
    EXPECT_EQ(this->buy(1, 10'000, 10), Status::Accepted);
    EXPECT_EQ(this->sell(1, 9'000, 10), Status::RejectedDuplicate);
    EXPECT_TRUE(this->trades.empty());  // the duplicate must not trade
    EXPECT_EQ(this->engine.book().orderCount(), 1u);
}

TYPED_TEST(MatchingEngineTest, InvalidAddRejected) {
    EXPECT_EQ(this->buy(1, 10'000, 0), Status::RejectedInvalid);
    EXPECT_EQ(this->buy(2, 0, 10), Status::RejectedInvalid);
    EXPECT_EQ(this->buy(3, -5, 10), Status::RejectedInvalid);
    EXPECT_EQ(this->engine.book().orderCount(), 0u);
    EXPECT_EQ(this->engine.stats().rejected, 3u);
}

TYPED_TEST(MatchingEngineTest, IncomingIntoEmptyBook) {
    this->buy(1, 10'000, 10);
    EXPECT_TRUE(this->trades.empty());
    EXPECT_EQ(this->engine.book().orderCount(), 1u);
}

TYPED_TEST(MatchingEngineTest, TradeIdsIncreaseAndStatsAddUp) {
    this->sell(1, 10'000, 10);
    this->sell(2, 10'001, 10);
    this->sell(3, 10'002, 10);
    this->buy(4, 10'002, 25);
    ASSERT_EQ(this->trades.size(), 3u);
    for (std::size_t i = 0; i < this->trades.size(); ++i)
        EXPECT_EQ(this->trades[i].tradeId, i + 1);
    EXPECT_EQ(this->engine.stats().trades, 3u);
    EXPECT_EQ(this->engine.stats().volume, 25u);
}

TYPED_TEST(MatchingEngineTest, ProcessDispatchesEvents) {
    this->engine.process(*parseEvent("ADD SELL 1 10000 100"), this->trades);
    this->engine.process(*parseEvent("MODIFY 1 10000 80"), this->trades);
    this->engine.process(*parseEvent("ADD BUY 2 10000 30"), this->trades);
    EXPECT_EQ(this->order(1)->remaining, 50u);
    EXPECT_EQ(this->engine.process(*parseEvent("CANCEL 1"), this->trades), Status::Cancelled);
    EXPECT_EQ(this->engine.book().orderCount(), 0u);
}
