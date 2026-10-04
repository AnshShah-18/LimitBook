// Integration tests: replay thousands of generated events and check the engine
// against (1) an independent brute-force reference model and (2) book invariants.

#include <gtest/gtest.h>

#include <algorithm>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "book_types.hpp"
#include "event_generator.hpp"
#include "matching_engine.hpp"

using namespace aquila;

namespace {

// Deliberately naive: a flat vector scanned linearly for the best order. Slow, but
// short enough to verify by eye, and it shares no code with the real books.
class ReferenceExchange {
public:
    struct Fill {
        OrderId buy, sell;
        Price price;
        Quantity qty;
        bool operator==(const Fill&) const = default;
    };

    std::vector<Fill> process(const Event& e) {
        std::vector<Fill> fills;
        auto it = findLive(e.id);
        switch (e.type) {
            case EventType::Add:
                if (e.quantity == 0 || e.price <= 0 || it != orders_.end()) break;
                addAndMatch(e.id, e.side, e.price, e.quantity, fills);
                break;
            case EventType::Cancel:
                if (it != orders_.end()) orders_.erase(it);
                break;
            case EventType::Modify:
                if (it == orders_.end() || e.quantity == 0 || e.price <= 0) break;
                if (e.price == it->price && e.quantity <= it->remaining) {
                    it->remaining = e.quantity;  // keeps its place in time
                } else {
                    const Side side = it->side;
                    orders_.erase(it);
                    addAndMatch(e.id, side, e.price, e.quantity, fills);
                }
                break;
        }
        return fills;
    }

    // Book as (side, price, id, remaining) in priority order.
    std::vector<std::tuple<Side, Price, OrderId, Quantity>> snapshot() const {
        std::vector<Order> sorted = orders_;
        std::sort(sorted.begin(), sorted.end(), [](const Order& a, const Order& b) {
            if (a.side != b.side) return a.side < b.side;
            if (a.price != b.price) return a.side == Side::Buy ? a.price > b.price : a.price < b.price;
            return a.seq < b.seq;
        });
        std::vector<std::tuple<Side, Price, OrderId, Quantity>> out;
        for (const auto& o : sorted) out.emplace_back(o.side, o.price, o.id, o.remaining);
        return out;
    }

private:
    std::vector<Order>::iterator findLive(OrderId id) {
        return std::find_if(orders_.begin(), orders_.end(), [&](const Order& o) { return o.id == id; });
    }

    void addAndMatch(OrderId id, Side side, Price price, Quantity qty, std::vector<Fill>& fills) {
        Quantity left = qty;
        while (left > 0) {
            auto best = orders_.end();
            for (auto it = orders_.begin(); it != orders_.end(); ++it) {
                if (it->side == side) continue;
                const bool crosses = side == Side::Buy ? it->price <= price : it->price >= price;
                if (!crosses) continue;
                if (best == orders_.end()) { best = it; continue; }
                const bool better = side == Side::Buy ? it->price < best->price : it->price > best->price;
                if (better || (it->price == best->price && it->seq < best->seq)) best = it;
            }
            if (best == orders_.end()) break;
            const Quantity q = std::min(left, best->remaining);
            left -= q;
            best->remaining -= q;
            fills.push_back(side == Side::Buy ? Fill{id, best->id, best->price, q}
                                              : Fill{best->id, id, best->price, q});
            if (best->remaining == 0) orders_.erase(best);
        }
        if (left > 0) orders_.push_back(Order{id, side, price, qty, left, ++seq_});
    }

    std::vector<Order> orders_;
    SeqNum seq_ = 0;
};

template <typename Book>
std::vector<std::tuple<Side, Price, OrderId, Quantity>> snapshotOf(const Book& book) {
    std::vector<std::tuple<Side, Price, OrderId, Quantity>> out;
    for (Side s : {Side::Buy, Side::Sell})
        book.forEachOrder(s, [&](const Order& o) { out.emplace_back(o.side, o.price, o.id, o.remaining); });
    return out;
}

template <typename Book>
void checkInvariants(const Book& book) {
    // When the engine is idle nothing is left that could trade: the book is never crossed.
    if (book.bestBid() && book.bestAsk()) {
        ASSERT_LT(*book.bestBid(), *book.bestAsk());
    }
    std::size_t counted = 0;
    for (Side s : {Side::Buy, Side::Sell}) {
        book.forEachOrder(s, [&](const Order& o) {
            ++counted;
            ASSERT_GT(o.remaining, 0u) << "filled order left resting: " << o.id;
            ASSERT_LE(o.remaining, o.quantity) << "remaining exceeds quantity: " << o.id;
            ASSERT_EQ(o.side, s);
        });
    }
    ASSERT_EQ(counted, book.orderCount());
}

}  // namespace

template <typename Book>
class IntegrationTest : public ::testing::Test {};
TYPED_TEST_SUITE(IntegrationTest, BookTypes);

TYPED_TEST(IntegrationTest, MatchesReferenceModelEventByEvent) {
    GeneratorConfig cfg;
    cfg.seed = 2024;
    cfg.maxLiveOrders = 800;  // keeps the O(n) reference model fast
    EventGenerator gen(cfg);
    const auto events = gen.generate(60'000);

    BasicMatchingEngine<TypeParam> engine;
    ReferenceExchange ref;
    std::vector<Trade> trades;

    for (std::size_t i = 0; i < events.size(); ++i) {
        trades.clear();
        engine.process(events[i], trades);
        const auto expected = ref.process(events[i]);

        ASSERT_EQ(trades.size(), expected.size()) << "event " << i << ": " << toString(events[i]);
        for (std::size_t k = 0; k < trades.size(); ++k) {
            const ReferenceExchange::Fill got{trades[k].buyOrderId, trades[k].sellOrderId,
                                              trades[k].price, trades[k].quantity};
            ASSERT_EQ(got, expected[k]) << "event " << i << " trade " << k;
        }
        if (i % 500 == 0 || i + 1 == events.size()) {
            ASSERT_EQ(snapshotOf(engine.book()), ref.snapshot()) << "book diverged at event " << i;
            checkInvariants(engine.book());
        }
    }
    EXPECT_GT(engine.stats().trades, 1'000u);  // the workload really exercises matching
}

TYPED_TEST(IntegrationTest, InvariantsHoldOverLargeReplay) {
    GeneratorConfig cfg;
    cfg.seed = 99;
    EventGenerator gen(cfg);
    const auto events = gen.generate(250'000);

    BasicMatchingEngine<TypeParam> engine;
    std::vector<Trade> trades;
    std::unordered_set<OrderId> cancelled;
    std::uint64_t tradeVolume = 0, tradeCount = 0, lastTradeId = 0;

    for (std::size_t i = 0; i < events.size(); ++i) {
        const Event& e = events[i];
        trades.clear();
        const Status st = engine.process(e, trades);
        if (e.type == EventType::Cancel && st == Status::Cancelled) cancelled.insert(e.id);

        for (const Trade& t : trades) {
            ASSERT_GT(t.quantity, 0u);
            ASSERT_EQ(t.tradeId, lastTradeId + 1);
            lastTradeId = t.tradeId;
            // The generator never reuses ids, so a cancelled id must never trade again.
            ASSERT_FALSE(cancelled.contains(t.buyOrderId)) << "cancelled order traded";
            ASSERT_FALSE(cancelled.contains(t.sellOrderId)) << "cancelled order traded";
            tradeVolume += t.quantity;
            ++tradeCount;
        }
        if (st == Status::Cancelled) {
            ASSERT_EQ(engine.book().find(e.id), nullptr);
        }
        if (engine.book().bestBid() && engine.book().bestAsk()) {
            ASSERT_LT(*engine.book().bestBid(), *engine.book().bestAsk()) << "crossed at " << i;
        }
        if (i % 10'000 == 0) checkInvariants(engine.book());
    }
    checkInvariants(engine.book());
    EXPECT_EQ(tradeCount, engine.stats().trades);
    EXPECT_EQ(tradeVolume, engine.stats().volume);
    EXPECT_EQ(engine.stats().events, events.size());
}
