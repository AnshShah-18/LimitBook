#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "event.hpp"

namespace aquila {

struct GeneratorConfig {
    std::uint64_t seed = 42;          // same seed -> identical event stream
    double addRatio = 0.60;           // remaining probability mass is split below
    double cancelRatio = 0.25;        // modify ratio = 1 - add - cancel
    double aggressiveRatio = 0.15;    // share of adds priced through the mid (likely to trade)
    Price startMid = 10'000;          // $100.00
    Price maxDepthTicks = 40;         // passive orders land within this many ticks of mid
    Quantity minQty = 10;
    Quantity maxQty = 500;
    std::size_t maxLiveOrders = 10'000;  // above this, cancels are forced to bound book size
    OrderId firstOrderId = 1;
};

// Synthetic market-event source. It tracks the orders it has sent so cancels and
// modifies usually target real ids. It cannot see fills, so some cancels hit orders
// that already traded; the engine reports those as NotFound, as a real venue would.
class EventGenerator {
public:
    explicit EventGenerator(GeneratorConfig cfg = {});

    Event next();
    std::vector<Event> generate(std::size_t count);

    // Passive, non-crossing adds that build a resting book of `count` orders
    // (bids below the mid, asks above). Used to set up book-size benchmarks.
    std::vector<Event> prefill(std::size_t count);

    [[nodiscard]] std::size_t liveCount() const noexcept { return live_.size(); }

private:
    struct Live {
        OrderId id;
        Side side;
        Price price;
        Quantity qty;
    };

    Event makeAdd(bool allowAggressive);
    Event makeCancel();
    Event makeModify();
    Price passivePrice(Side side);
    Quantity randomQty();

    GeneratorConfig cfg_;
    std::mt19937_64 rng_;
    std::uniform_real_distribution<double> unit_{0.0, 1.0};
    std::vector<Live> live_;
    Price mid_;
    OrderId nextId_;
};

}  // namespace aquila
