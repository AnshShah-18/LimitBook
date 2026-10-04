// End-to-end matching-engine benchmarks: replay full synthetic event streams.
//
//  BM_Replay           throughput for 10K / 100K / 1M events
//  BM_ReplayLatency    same workloads, every event timed; reports mean/p50/p95/p99
//  BM_ReplayBookSize   100K events against a book held near 100 / 1K / 10K orders

#include <benchmark/benchmark.h>

#include <vector>

#include "bench_common.hpp"
#include "latency.hpp"

using namespace aquila;

namespace {

template <typename Engine>
void replay(benchmark::State& state, const std::vector<Event>& events) {
    std::vector<Trade> trades;
    trades.reserve(1024);
    std::uint64_t totalTrades = 0;
    for (auto _ : state) {
        state.PauseTiming();
        Engine engine;  // fresh book each iteration
        engine.book().reserve(20'000);
        state.ResumeTiming();
        for (const Event& e : events) {
            engine.process(e, trades);
            trades.clear();
        }
        totalTrades = engine.stats().trades;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(events.size()));
    state.counters["events_per_sec"] = benchmark::Counter(
        static_cast<double>(state.iterations() * events.size()), benchmark::Counter::kIsRate);
    state.counters["trades"] = static_cast<double>(totalTrades);
}

template <typename Engine>
void BM_Replay(benchmark::State& state) {
    replay<Engine>(state, bench::cachedEvents(static_cast<std::size_t>(state.range(0))));
}

template <typename Engine>
void BM_ReplayBookSize(benchmark::State& state) {
    const auto size = static_cast<std::size_t>(state.range(0));
    replay<Engine>(state, bench::cachedEvents(100'000, size));
}

// Per-event latency. Each sample includes one steady_clock read (~20 ns on Linux,
// see README) so absolute values are slightly pessimistic; relative comparisons hold.
template <typename Engine>
void BM_ReplayLatency(benchmark::State& state) {
    const auto& events = bench::cachedEvents(static_cast<std::size_t>(state.range(0)));
    std::vector<Trade> trades;
    trades.reserve(1024);
    LatencySummary s;
    for (auto _ : state) {
        state.PauseTiming();
        Engine engine;
        engine.book().reserve(20'000);
        LatencyRecorder rec(events.size());
        state.ResumeTiming();
        for (const Event& e : events) {
            const auto t0 = nowNs();
            engine.process(e, trades);
            rec.record(nowNs() - t0);
            trades.clear();
        }
        state.PauseTiming();
        s = rec.summarize();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(events.size()));
    state.counters["mean_ns"] = s.meanNs;
    state.counters["p50_ns"] = static_cast<double>(s.p50Ns);
    state.counters["p95_ns"] = static_cast<double>(s.p95Ns);
    state.counters["p99_ns"] = static_cast<double>(s.p99Ns);
    state.counters["p999_ns"] = static_cast<double>(s.p999Ns);
}

// Cost of the timer itself, so latency numbers can be read honestly.
void BM_ClockOverhead(benchmark::State& state) {
    for (auto _ : state) benchmark::DoNotOptimize(nowNs());
}

}  // namespace

#define WORKLOADS ->Arg(10'000)->Arg(100'000)->Arg(1'000'000)->Unit(benchmark::kMillisecond)
BENCHMARK_TEMPLATE(BM_Replay, BaselineEngine) WORKLOADS;
BENCHMARK_TEMPLATE(BM_Replay, PooledEngine) WORKLOADS;
BENCHMARK_TEMPLATE(BM_ReplayLatency, BaselineEngine) WORKLOADS->Iterations(3);
BENCHMARK_TEMPLATE(BM_ReplayLatency, PooledEngine) WORKLOADS->Iterations(3);

#define BOOK_SIZES ->Arg(100)->Arg(1'000)->Arg(10'000)->Unit(benchmark::kMillisecond)
BENCHMARK_TEMPLATE(BM_ReplayBookSize, BaselineEngine) BOOK_SIZES;
BENCHMARK_TEMPLATE(BM_ReplayBookSize, PooledEngine) BOOK_SIZES;

BENCHMARK(BM_ClockOverhead);

BENCHMARK_MAIN();
