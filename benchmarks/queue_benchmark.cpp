// Concurrency experiment: std::mutex + condition_variable queue vs lock-free SPSC ring.
//
//  BM_QueueTransfer    raw handoff of 1M integers between two threads
//  BM_Pipeline         generator thread -> queue -> matching engine, 1M events

#include <benchmark/benchmark.h>

#include <cstdint>
#include <thread>

#include "bench_common.hpp"
#include "event_queue.hpp"
#include "pipeline.hpp"

using namespace aquila;

namespace {

template <typename Queue>
void BM_QueueTransfer(benchmark::State& state) {
    constexpr std::uint64_t kItems = 1'000'000;
    for (auto _ : state) {
        Queue q(static_cast<std::size_t>(state.range(0)));
        std::uint64_t sum = 0;
        std::thread consumer([&] {
            std::uint64_t v;
            while (q.pop(v)) sum += v;
        });
        for (std::uint64_t i = 0; i < kItems; ++i) q.push(i);
        q.close();
        consumer.join();
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * kItems);
}

template <typename Queue>
void BM_Pipeline(benchmark::State& state) {
    const auto& events = bench::cachedEvents(1'000'000);
    RunResult r;
    for (auto _ : state) {
        PooledEngine engine;
        engine.book().reserve(20'000);
        r = runPipeline<Queue>(engine, events, static_cast<std::size_t>(state.range(0)));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(events.size()));
    // Saturated pipeline: latency here is dominated by time spent waiting in a full queue.
    state.counters["p50_ns"] = static_cast<double>(r.latency.p50Ns);
    state.counters["p99_ns"] = static_cast<double>(r.latency.p99Ns);
}

}  // namespace

BENCHMARK_TEMPLATE(BM_QueueTransfer, MutexQueue<std::uint64_t>)
    ->Arg(1'024)->Arg(65'536)->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_QueueTransfer, SpscRingBuffer<std::uint64_t>)
    ->Arg(1'024)->Arg(65'536)->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Pipeline, MutexQueue<TimedEvent>)
    ->Arg(65'536)->UseRealTime()->Unit(benchmark::kMillisecond);
BENCHMARK_TEMPLATE(BM_Pipeline, SpscRingBuffer<TimedEvent>)
    ->Arg(65'536)->UseRealTime()->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
