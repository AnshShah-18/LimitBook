// Order-book micro-benchmarks at different resting-book sizes (100 / 1K / 10K orders).
// Each benchmark keeps the book size constant so the measurement is "cost of one
// operation against a book of N orders".

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

#include "bench_common.hpp"

using namespace limitbook;

namespace {

template <typename Book>
struct PrefilledBook {
    Book book;
    std::vector<Order> resting;
    explicit PrefilledBook(std::size_t n) {
        GeneratorConfig cfg;
        cfg.maxLiveOrders = n + 1;
        EventGenerator gen(cfg);
        book.reserve(n * 2);
        SeqNum seq = 0;
        for (const Event& e : gen.prefill(n)) {
            Order o{e.id, e.side, e.price, e.quantity, e.quantity, ++seq};
            book.add(o);
            resting.push_back(o);
        }
    }
};

// Add a new passive order, then cancel it (it sits at the back of its level).
template <typename Book>
void BM_AddThenCancel(benchmark::State& state) {
    PrefilledBook<Book> pb(static_cast<std::size_t>(state.range(0)));
    std::mt19937_64 rng(7);
    OrderId nextId = 1'000'000'000;
    SeqNum seq = 1'000'000'000;
    for (auto _ : state) {
        const Order& tmpl = pb.resting[rng() % pb.resting.size()];
        const OrderId id = nextId++;
        benchmark::DoNotOptimize(pb.book.add(Order{id, tmpl.side, tmpl.price, 100, 100, ++seq}));
        benchmark::DoNotOptimize(pb.book.cancel(id));
    }
    state.SetItemsProcessed(state.iterations() * 2);
}

// Cancel a random resting order (usually mid-queue) and immediately re-add it.
template <typename Book>
void BM_CancelRandomThenReAdd(benchmark::State& state) {
    PrefilledBook<Book> pb(static_cast<std::size_t>(state.range(0)));
    std::mt19937_64 rng(11);
    SeqNum seq = 1'000'000'000;
    for (auto _ : state) {
        Order& o = pb.resting[rng() % pb.resting.size()];
        benchmark::DoNotOptimize(pb.book.cancel(o.id));
        o.seq = ++seq;
        benchmark::DoNotOptimize(pb.book.add(o));
    }
    state.SetItemsProcessed(state.iterations() * 2);
}

// Top-of-book query: the operation the matcher performs on every incoming order.
template <typename Book>
void BM_BestBidAsk(benchmark::State& state) {
    PrefilledBook<Book> pb(static_cast<std::size_t>(state.range(0)));
    for (auto _ : state) {
        benchmark::DoNotOptimize(pb.book.bestBid());
        benchmark::DoNotOptimize(pb.book.bestAsk());
        benchmark::DoNotOptimize(pb.book.best(Side::Buy));
    }
    state.SetItemsProcessed(state.iterations());
}

}  // namespace

#define BOOK_SIZES ->Arg(100)->Arg(1'000)->Arg(10'000)
BENCHMARK_TEMPLATE(BM_AddThenCancel, MapListOrderBook) BOOK_SIZES;
BENCHMARK_TEMPLATE(BM_AddThenCancel, PooledOrderBook) BOOK_SIZES;
BENCHMARK_TEMPLATE(BM_CancelRandomThenReAdd, MapListOrderBook) BOOK_SIZES;
BENCHMARK_TEMPLATE(BM_CancelRandomThenReAdd, PooledOrderBook) BOOK_SIZES;
BENCHMARK_TEMPLATE(BM_BestBidAsk, MapListOrderBook) BOOK_SIZES;
BENCHMARK_TEMPLATE(BM_BestBidAsk, PooledOrderBook) BOOK_SIZES;

BENCHMARK_MAIN();
