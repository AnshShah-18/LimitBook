// Counts heap allocations per processed event for each order-book implementation,
// by replacing the global operator new/delete. Complements callgrind/perf: those show
// *where* time goes; this shows *how many* allocations the hot path performs.
//
//   ./alloc_profile [events]

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "event_generator.hpp"
#include "matching_engine.hpp"

namespace {
std::atomic<std::uint64_t> gAllocs{0};
std::atomic<std::uint64_t> gBytes{0};
}  // namespace

void* operator new(std::size_t n) {
    gAllocs.fetch_add(1, std::memory_order_relaxed);
    gBytes.fetch_add(n, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace aquila;

template <typename Engine>
void measure(const char* name, const std::vector<Event>& events, std::size_t warmup) {
    Engine engine;
    engine.book().reserve(20'000);
    std::vector<Trade> trades;
    trades.reserve(1024);
    for (std::size_t i = 0; i < warmup; ++i) {
        engine.process(events[i], trades);
        trades.clear();
    }
    const auto a0 = gAllocs.load(), b0 = gBytes.load();
    for (std::size_t i = warmup; i < events.size(); ++i) {
        engine.process(events[i], trades);
        trades.clear();
    }
    const double n = static_cast<double>(events.size() - warmup);
    std::printf("%-10s allocations/event: %6.3f   bytes/event: %7.1f   (steady state, %zu events)\n",
                name, (gAllocs.load() - a0) / n, (gBytes.load() - b0) / n, events.size() - warmup);
}

int main(int argc, char** argv) {
    const std::size_t count = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1'000'000;
    EventGenerator gen;
    const auto events = gen.generate(count);
    const std::size_t warmup = count / 10;  // let pools / hash tables reach steady size
    measure<BaselineEngine>("baseline", events, warmup);
    measure<PooledEngine>("pooled", events, warmup);
}
