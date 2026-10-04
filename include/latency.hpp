#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace aquila {

using Clock = std::chrono::steady_clock;

inline std::uint64_t nowNs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
            .count());
}

struct LatencySummary {
    std::uint64_t count = 0;
    double meanNs = 0;
    std::uint64_t p50Ns = 0, p95Ns = 0, p99Ns = 0, p999Ns = 0, maxNs = 0;
};

// Collects one latency sample per event (preallocated so recording never allocates)
// and reports exact order statistics.
class LatencyRecorder {
public:
    explicit LatencyRecorder(std::size_t expected = 0) { samples_.reserve(expected); }
    void record(std::uint64_t ns) { samples_.push_back(ns); }
    [[nodiscard]] std::size_t size() const noexcept { return samples_.size(); }

    [[nodiscard]] LatencySummary summarize() const {
        LatencySummary s;
        if (samples_.empty()) return s;
        std::vector<std::uint64_t> v = samples_;
        std::sort(v.begin(), v.end());
        auto pct = [&](double p) {
            const auto idx = static_cast<std::size_t>(p * static_cast<double>(v.size() - 1));
            return v[idx];
        };
        long double total = 0;
        for (auto x : v) total += x;
        s.count = v.size();
        s.meanNs = static_cast<double>(total / v.size());
        s.p50Ns = pct(0.50);
        s.p95Ns = pct(0.95);
        s.p99Ns = pct(0.99);
        s.p999Ns = pct(0.999);
        s.maxNs = v.back();
        return s;
    }

private:
    std::vector<std::uint64_t> samples_;
};

}  // namespace aquila
