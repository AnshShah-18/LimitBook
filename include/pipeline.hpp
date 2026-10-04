#pragma once

#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include "event.hpp"
#include "event_queue.hpp"
#include "latency.hpp"
#include "matching_engine.hpp"

namespace limitbook {

struct RunResult {
    std::uint64_t events = 0;
    std::uint64_t trades = 0;
    std::uint64_t volume = 0;
    double seconds = 0;
    LatencySummary latency;
    [[nodiscard]] double eventsPerSec() const { return seconds > 0 ? events / seconds : 0; }
};

// Single-threaded replay. With measureLatency, each event is timed individually:
// latency = time spent inside engine.process() (the engine's service time). Timing
// costs ~25 ns per event, so throughput should be taken from an untimed run.
template <typename Engine>
RunResult runSingleThreaded(Engine& engine, std::span<const Event> events,
                            bool measureLatency = true) {
    LatencyRecorder rec(measureLatency ? events.size() : 0);
    std::vector<Trade> trades;
    trades.reserve(1024);
    const std::uint64_t start = nowNs();
    if (measureLatency) {
        for (const Event& e : events) {
            const std::uint64_t t0 = nowNs();
            engine.process(e, trades);
            rec.record(nowNs() - t0);
            trades.clear();  // a real system would publish these; we only count them
        }
    } else {
        for (const Event& e : events) {
            engine.process(e, trades);
            trades.clear();
        }
    }
    const std::uint64_t end = nowNs();
    return {events.size(), engine.stats().trades, engine.stats().volume,
            static_cast<double>(end - start) / 1e9, rec.summarize()};
}

// Event stamped by the producer when it enters the queue.
struct TimedEvent {
    Event event;
    std::uint64_t enqueuedNs = 0;
};

// Producer/consumer pipeline: generator thread -> Queue -> matching-engine thread.
// Latency = enqueue timestamp to "engine finished processing", so it includes time
// spent waiting in the queue. That is the number a client of the engine would see.
template <typename Queue, typename Engine>
    requires BlockingQueue<Queue, TimedEvent>
RunResult runPipeline(Engine& engine, std::span<const Event> events,
                      std::size_t queueCapacity = 65'536, double ratePerSec = 0) {
    Queue queue(queueCapacity);
    LatencyRecorder rec(events.size());
    const std::uint64_t start = nowNs();

    std::thread consumer([&] {
        std::vector<Trade> trades;
        trades.reserve(1024);
        TimedEvent te;
        while (queue.pop(te)) {
            engine.process(te.event, trades);
            rec.record(nowNs() - te.enqueuedNs);
            trades.clear();
        }
    });

    if (ratePerSec <= 0) {
        // Saturation mode: push as fast as possible. Measures max throughput; latency
        // then mostly reflects how long events sit in a full queue.
        for (const Event& e : events) queue.push(TimedEvent{e, nowNs()});
    } else {
        // Paced mode: emit at a fixed offered load (busy-wait to the next slot), which
        // gives meaningful latency below saturation.
        const double gapNs = 1e9 / ratePerSec;
        for (std::size_t i = 0; i < events.size(); ++i) {
            const auto due = start + static_cast<std::uint64_t>(static_cast<double>(i) * gapNs);
            while (nowNs() < due) {
            }
            queue.push(TimedEvent{events[i], nowNs()});
        }
    }
    queue.close();
    consumer.join();

    const std::uint64_t end = nowNs();
    return {events.size(), engine.stats().trades, engine.stats().volume,
            static_cast<double>(end - start) / 1e9, rec.summarize()};
}

}  // namespace limitbook
