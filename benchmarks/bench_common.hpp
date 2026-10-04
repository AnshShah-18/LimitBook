#pragma once

#include <cstdint>
#include <vector>

#include "event_generator.hpp"
#include "matching_engine.hpp"

namespace aquila::bench {

// Events are generated once per configuration and cached, so benchmark time only
// covers the engine, never the generator.
inline const std::vector<Event>& cachedEvents(std::size_t count, std::size_t maxLive = 10'000,
                                              std::uint64_t seed = 42) {
    struct Key {
        std::size_t count, maxLive;
        std::uint64_t seed;
        std::vector<Event> events;
    };
    static std::vector<Key> cache;
    for (auto& k : cache)
        if (k.count == count && k.maxLive == maxLive && k.seed == seed) return k.events;
    GeneratorConfig cfg;
    cfg.seed = seed;
    cfg.maxLiveOrders = maxLive;
    EventGenerator gen(cfg);
    cache.push_back({count, maxLive, seed, gen.generate(count)});
    return cache.back().events;
}

}  // namespace aquila::bench
