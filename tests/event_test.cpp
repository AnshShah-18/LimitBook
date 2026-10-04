#include <gtest/gtest.h>

#include <set>

#include "event_generator.hpp"
#include "matching_engine.hpp"

using namespace aquila;

TEST(PriceTest, ParsesDecimalToTicks) {
    Price p{};
    ASSERT_TRUE(parsePrice("101.25", p));
    EXPECT_EQ(p, 10'125);
    ASSERT_TRUE(parsePrice("101.2", p));
    EXPECT_EQ(p, 10'120);
    ASSERT_TRUE(parsePrice("7", p));
    EXPECT_EQ(p, 700);
    EXPECT_FALSE(parsePrice("1.234", p));
    EXPECT_FALSE(parsePrice("abc", p));
    EXPECT_FALSE(parsePrice("", p));
    EXPECT_FALSE(parsePrice("1.2.3", p));
    EXPECT_EQ(formatPrice(10'125), "101.25");
    EXPECT_EQ(formatPrice(10'005), "100.05");
}

TEST(EventTest, ParseSpecExample) {
    auto e = parseEvent("ADD BUY 10001 10125 200");
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(e->type, EventType::Add);
    EXPECT_EQ(e->side, Side::Buy);
    EXPECT_EQ(e->id, 10'001u);
    EXPECT_EQ(e->price, 10'125);
    EXPECT_EQ(e->quantity, 200u);
}

TEST(EventTest, RoundTrip) {
    for (const char* line : {"ADD SELL 5 9999 1", "CANCEL 77", "MODIFY 3 10010 40"}) {
        auto e = parseEvent(line);
        ASSERT_TRUE(e.has_value()) << line;
        EXPECT_EQ(toString(*e), line);
    }
}

TEST(EventTest, RejectsMalformed) {
    EXPECT_FALSE(parseEvent("").has_value());
    EXPECT_FALSE(parseEvent("ADD HOLD 1 100 1").has_value());
    EXPECT_FALSE(parseEvent("ADD BUY 1 100").has_value());
    EXPECT_FALSE(parseEvent("CANCEL x").has_value());
    EXPECT_FALSE(parseEvent("ADD BUY 1 100 -1").has_value());
    EXPECT_FALSE(parseEvent("FOO 1").has_value());
}

TEST(GeneratorTest, SameSeedSameStream) {
    GeneratorConfig cfg;
    cfg.seed = 7;
    EventGenerator a(cfg), b(cfg);
    EXPECT_EQ(a.generate(5'000), b.generate(5'000));
}

TEST(GeneratorTest, DifferentSeedDifferentStream) {
    GeneratorConfig c1, c2;
    c1.seed = 1;
    c2.seed = 2;
    EXPECT_NE(EventGenerator(c1).generate(1'000), EventGenerator(c2).generate(1'000));
}

TEST(GeneratorTest, ProducesMixAndUniqueAddIds) {
    EventGenerator gen;
    auto events = gen.generate(50'000);
    std::size_t adds = 0, cancels = 0, modifies = 0;
    std::set<OrderId> addIds;
    for (const auto& e : events) {
        if (e.type == EventType::Add) {
            ++adds;
            EXPECT_TRUE(addIds.insert(e.id).second) << "duplicate add id " << e.id;
            EXPECT_GT(e.price, 0);
            EXPECT_GT(e.quantity, 0u);
        } else if (e.type == EventType::Cancel) {
            ++cancels;
        } else {
            ++modifies;
        }
    }
    EXPECT_GT(adds, events.size() / 2);
    EXPECT_GT(cancels, events.size() / 10);
    EXPECT_GT(modifies, events.size() / 20);
}

TEST(GeneratorTest, LiveOrdersBounded) {
    GeneratorConfig cfg;
    cfg.maxLiveOrders = 500;
    EventGenerator gen(cfg);
    gen.generate(20'000);
    EXPECT_LE(gen.liveCount(), 500u);
}

TEST(GeneratorTest, PrefillBuildsNonCrossingBook) {
    EventGenerator gen;
    BaselineEngine engine;
    std::vector<Trade> trades;
    for (const auto& e : gen.prefill(2'000)) engine.process(e, trades);
    EXPECT_TRUE(trades.empty());
    EXPECT_EQ(engine.book().orderCount(), 2'000u);
    EXPECT_LT(*engine.book().bestBid(), *engine.book().bestAsk());
}
