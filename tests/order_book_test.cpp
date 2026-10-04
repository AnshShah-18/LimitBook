#include <gtest/gtest.h>

#include <vector>

#include "book_types.hpp"

using namespace aquila;

template <typename Book>
class OrderBookTest : public ::testing::Test {
protected:
    Book book;
    SeqNum seq = 0;
    bool add(OrderId id, Side side, Price price, Quantity qty) {
        return book.add(Order{id, side, price, qty, qty, ++seq});
    }
    std::vector<OrderId> ids(Side side) {
        std::vector<OrderId> out;
        book.forEachOrder(side, [&](const Order& o) { out.push_back(o.id); });
        return out;
    }
};
TYPED_TEST_SUITE(OrderBookTest, BookTypes);

TYPED_TEST(OrderBookTest, EmptyBook) {
    EXPECT_EQ(this->book.orderCount(), 0u);
    EXPECT_FALSE(this->book.bestBid().has_value());
    EXPECT_FALSE(this->book.bestAsk().has_value());
    EXPECT_EQ(this->book.best(Side::Buy), nullptr);
    EXPECT_EQ(this->book.best(Side::Sell), nullptr);
    EXPECT_EQ(this->book.find(1), nullptr);
}

TYPED_TEST(OrderBookTest, InsertBuyOrders) {
    EXPECT_TRUE(this->add(1, Side::Buy, 10'000, 100));
    EXPECT_TRUE(this->add(2, Side::Buy, 10'010, 50));
    EXPECT_EQ(this->book.orderCount(), 2u);
    EXPECT_EQ(this->book.levelCount(Side::Buy), 2u);
    EXPECT_EQ(this->book.levelCount(Side::Sell), 0u);
    ASSERT_NE(this->book.find(1), nullptr);
    EXPECT_EQ(this->book.find(1)->remaining, 100u);
}

TYPED_TEST(OrderBookTest, InsertSellOrders) {
    EXPECT_TRUE(this->add(1, Side::Sell, 10'100, 100));
    EXPECT_TRUE(this->add(2, Side::Sell, 10'050, 75));
    EXPECT_EQ(this->book.levelCount(Side::Sell), 2u);
    EXPECT_EQ(this->book.find(2)->side, Side::Sell);
}

TYPED_TEST(OrderBookTest, BestBidIsHighestPrice) {
    this->add(1, Side::Buy, 10'120, 100);
    this->add(2, Side::Buy, 10'125, 100);
    this->add(3, Side::Buy, 10'100, 100);
    EXPECT_EQ(this->book.bestBid(), 10'125);
    EXPECT_EQ(this->book.best(Side::Buy)->id, 2u);
}

TYPED_TEST(OrderBookTest, BestAskIsLowestPrice) {
    this->add(1, Side::Sell, 10'130, 100);
    this->add(2, Side::Sell, 10'126, 100);
    this->add(3, Side::Sell, 10'200, 100);
    EXPECT_EQ(this->book.bestAsk(), 10'126);
    EXPECT_EQ(this->book.best(Side::Sell)->id, 2u);
}

TYPED_TEST(OrderBookTest, PriceTimePriorityOrdering) {
    // Spec example: 100 @ 101.25, 200 @ 101.25, 300 @ 101.20.
    this->add(1, Side::Buy, 10'125, 100);
    this->add(2, Side::Buy, 10'125, 200);
    this->add(3, Side::Buy, 10'120, 300);
    EXPECT_EQ(this->ids(Side::Buy), (std::vector<OrderId>{1, 2, 3}));
    EXPECT_EQ(this->book.depthAt(Side::Buy, 10'125), 300u);
}

TYPED_TEST(OrderBookTest, PopBestWalksFifoThenNextLevel) {
    this->add(1, Side::Sell, 10'000, 10);
    this->add(2, Side::Sell, 10'000, 10);
    this->add(3, Side::Sell, 10'001, 10);
    std::vector<OrderId> popped;
    while (Order* o = this->book.best(Side::Sell)) {
        popped.push_back(o->id);
        o->remaining = 0;
        this->book.popBest(Side::Sell);
    }
    EXPECT_EQ(popped, (std::vector<OrderId>{1, 2, 3}));
    EXPECT_EQ(this->book.orderCount(), 0u);
    EXPECT_EQ(this->book.levelCount(Side::Sell), 0u);
}

TYPED_TEST(OrderBookTest, CancelFromMiddleOfLevel) {
    this->add(1, Side::Buy, 10'000, 10);
    this->add(2, Side::Buy, 10'000, 20);
    this->add(3, Side::Buy, 10'000, 30);
    EXPECT_TRUE(this->book.cancel(2));
    EXPECT_EQ(this->ids(Side::Buy), (std::vector<OrderId>{1, 3}));
    EXPECT_EQ(this->book.find(2), nullptr);
    EXPECT_EQ(this->book.depthAt(Side::Buy, 10'000), 40u);
}

TYPED_TEST(OrderBookTest, CancelLastOrderRemovesLevel) {
    this->add(1, Side::Buy, 10'000, 10);
    this->add(2, Side::Buy, 9'990, 10);
    EXPECT_TRUE(this->book.cancel(1));
    EXPECT_EQ(this->book.levelCount(Side::Buy), 1u);
    EXPECT_EQ(this->book.bestBid(), 9'990);
}

TYPED_TEST(OrderBookTest, CancelUnknownIdFails) {
    EXPECT_FALSE(this->book.cancel(42));
    this->add(1, Side::Sell, 10'000, 10);
    EXPECT_TRUE(this->book.cancel(1));
    EXPECT_FALSE(this->book.cancel(1));  // already gone
}

TYPED_TEST(OrderBookTest, DuplicateIdRejected) {
    EXPECT_TRUE(this->add(7, Side::Buy, 10'000, 10));
    EXPECT_FALSE(this->add(7, Side::Sell, 10'500, 10));
    EXPECT_EQ(this->book.orderCount(), 1u);
    EXPECT_EQ(this->book.levelCount(Side::Sell), 0u);
}

TYPED_TEST(OrderBookTest, ReduceKeepsPositionAndFilledAmount) {
    this->add(1, Side::Buy, 10'000, 100);
    this->add(2, Side::Buy, 10'000, 100);
    EXPECT_TRUE(this->book.reduceTo(1, 40));
    EXPECT_EQ(this->book.find(1)->remaining, 40u);
    EXPECT_EQ(this->book.find(1)->filled(), 0u);
    EXPECT_EQ(this->ids(Side::Buy), (std::vector<OrderId>{1, 2}));
    EXPECT_FALSE(this->book.reduceTo(1, 50));   // cannot grow
    EXPECT_FALSE(this->book.reduceTo(1, 0));    // use cancel instead
    EXPECT_FALSE(this->book.reduceTo(99, 1));   // unknown
}

TYPED_TEST(OrderBookTest, IdCanBeReusedAfterCancel) {
    this->add(1, Side::Buy, 10'000, 10);
    this->book.cancel(1);
    EXPECT_TRUE(this->add(1, Side::Sell, 10'100, 5));
    EXPECT_EQ(this->book.find(1)->side, Side::Sell);
}

// ------------------------------------------------------------- FlatIdMap
#include <random>
#include <unordered_map>

#include "flat_id_map.hpp"

TEST(FlatIdMapTest, BasicOperations) {
    FlatIdMap m;
    EXPECT_EQ(m.find(5), FlatIdMap::kNone);
    EXPECT_TRUE(m.insert(5, 50));
    EXPECT_FALSE(m.insert(5, 51));
    EXPECT_EQ(m.find(5), 50u);
    EXPECT_TRUE(m.erase(5));
    EXPECT_FALSE(m.erase(5));
    EXPECT_EQ(m.find(5), FlatIdMap::kNone);
    EXPECT_EQ(m.size(), 0u);
}

// Randomized differential test against std::unordered_map, with heavy churn so
// backward-shift deletion and growth are both exercised.
TEST(FlatIdMapTest, MatchesUnorderedMapUnderChurn) {
    FlatIdMap m;
    std::unordered_map<OrderId, std::uint32_t> ref;
    std::mt19937_64 rng(3);
    for (int step = 0; step < 400'000; ++step) {
        const OrderId key = rng() % 5'000;  // small key space -> many collisions/reuses
        const auto op = rng() % 3;
        if (op == 0) {
            const auto v = static_cast<std::uint32_t>(rng() % 1'000'000);
            ASSERT_EQ(m.insert(key, v), ref.emplace(key, v).second);
        } else if (op == 1) {
            ASSERT_EQ(m.erase(key), ref.erase(key) == 1);
        } else {
            auto it = ref.find(key);
            ASSERT_EQ(m.find(key), it == ref.end() ? FlatIdMap::kNone : it->second);
        }
        ASSERT_EQ(m.size(), ref.size());
    }
    for (const auto& [k, v] : ref) ASSERT_EQ(m.find(k), v);
}
