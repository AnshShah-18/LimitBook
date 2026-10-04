#include "event_generator.hpp"

namespace aquila {

namespace {
// std::uniform_*_distribution output differs between standard libraries. These
// helpers map raw mt19937_64 output directly so a seed reproduces the same stream
// with GCC, Clang/libc++ and MSVC.
double toUnit(std::uint64_t x) { return static_cast<double>(x >> 11) * 0x1.0p-53; }
}  // namespace

EventGenerator::EventGenerator(GeneratorConfig cfg)
    : cfg_(cfg), rng_(cfg.seed), mid_(cfg.startMid), nextId_(cfg.firstOrderId) {
    live_.reserve(cfg_.maxLiveOrders + 1);
}

Quantity EventGenerator::randomQty() {
    const std::uint64_t span = cfg_.maxQty - cfg_.minQty + 1;
    return cfg_.minQty + static_cast<Quantity>(rng_() % span);
}

Price EventGenerator::passivePrice(Side side) {
    // Squaring a uniform draw concentrates orders near the top of book, like real depth.
    const double u = toUnit(rng_());
    const Price offset = 1 + static_cast<Price>(u * u * static_cast<double>(cfg_.maxDepthTicks));
    const Price p = side == Side::Buy ? mid_ - offset : mid_ + offset;
    return p > 0 ? p : 1;
}

Event EventGenerator::makeAdd(bool allowAggressive) {
    // The mid drifts as a slow random walk so the book keeps moving. Prefill keeps it
    // fixed so a resting book never crosses itself.
    if (allowAggressive && toUnit(rng_()) < 0.05) mid_ += (rng_() & 1) ? 1 : -1;
    if (mid_ <= cfg_.maxDepthTicks + 1) mid_ = cfg_.maxDepthTicks + 2;

    Event e;
    e.type = EventType::Add;
    e.side = (rng_() & 1) ? Side::Buy : Side::Sell;
    e.id = nextId_++;
    e.quantity = randomQty();
    if (allowAggressive && toUnit(rng_()) < cfg_.aggressiveRatio) {
        const Price through = 1 + static_cast<Price>(rng_() % 5);  // crosses the spread
        e.price = e.side == Side::Buy ? mid_ + through : mid_ - through;
    } else {
        e.price = passivePrice(e.side);
    }
    live_.push_back({e.id, e.side, e.price, e.quantity});
    return e;
}

Event EventGenerator::makeCancel() {
    const std::size_t i = rng_() % live_.size();
    Event e;
    e.type = EventType::Cancel;
    e.id = live_[i].id;
    live_[i] = live_.back();  // O(1) swap-remove
    live_.pop_back();
    return e;
}

Event EventGenerator::makeModify() {
    Live& o = live_[rng_() % live_.size()];
    Event e;
    e.type = EventType::Modify;
    e.id = o.id;
    if (rng_() & 1) {
        // Quantity reduction at the same price: keeps queue priority.
        e.price = o.price;
        e.quantity = o.qty > 1 ? 1 + static_cast<Quantity>(rng_() % (o.qty - 1)) : 1;
    } else {
        // Re-price by a few ticks: loses priority, may cross.
        const Price move = 1 + static_cast<Price>(rng_() % 3);
        e.price = (rng_() & 1) ? o.price + move : o.price - move;
        if (e.price <= 0) e.price = 1;
        e.quantity = randomQty();
    }
    o.price = e.price;
    o.qty = e.quantity;
    return e;
}

Event EventGenerator::next() {
    if (live_.empty()) return makeAdd(true);
    if (live_.size() >= cfg_.maxLiveOrders) return makeCancel();
    const double r = toUnit(rng_());
    if (r < cfg_.addRatio) return makeAdd(true);
    if (r < cfg_.addRatio + cfg_.cancelRatio) return makeCancel();
    return makeModify();
}

std::vector<Event> EventGenerator::generate(std::size_t count) {
    std::vector<Event> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.push_back(next());
    return out;
}

std::vector<Event> EventGenerator::prefill(std::size_t count) {
    std::vector<Event> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.push_back(makeAdd(false));
    return out;
}

}  // namespace aquila
