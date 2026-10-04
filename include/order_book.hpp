#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>

#include "order.hpp"

namespace limitbook {

// The contract every order-book implementation satisfies. The matching engine is
// templated on it, so alternative data structures can be swapped in, tested by the
// same suite and benchmarked head to head.
template <typename B>
concept OrderBookLike = requires(B b, const B cb, const Order& o, OrderId id, Side s,
                                 Price p, Quantity q, std::size_t n) {
    { b.add(o) } -> std::same_as<bool>;            // rest an order; false on duplicate id
    { b.cancel(id) } -> std::same_as<bool>;        // false if id unknown
    { b.reduceTo(id, q) } -> std::same_as<bool>;   // shrink remaining in place, keeps priority
    { cb.find(id) } -> std::same_as<const Order*>;
    { b.best(s) } -> std::same_as<Order*>;         // oldest order at the best price, or nullptr
    { b.popBest(s) } -> std::same_as<void>;        // remove that order
    { cb.bestBid() } -> std::same_as<std::optional<Price>>;
    { cb.bestAsk() } -> std::same_as<std::optional<Price>>;
    { cb.orderCount() } -> std::same_as<std::size_t>;
    { cb.levelCount(s) } -> std::same_as<std::size_t>;
    { cb.depthAt(s, p) } -> std::same_as<Quantity>;
    { b.reserve(n) } -> std::same_as<void>;
};

// Baseline (Phase 1) implementation: the "textbook" design.
//   price levels : std::map  (bids ordered high->low, asks low->high), O(log L)
//   FIFO queue   : std::list<Order> per level, O(1) push_back / erase by iterator
//   id lookup    : std::unordered_map<OrderId, locator>, O(1) average
// Every order costs three heap allocations (list node, hash node, and sometimes a
// map node), which is what the optimized PooledOrderBook attacks.
class MapListOrderBook {
public:
    bool add(const Order& order);
    bool cancel(OrderId id);
    bool reduceTo(OrderId id, Quantity newRemaining);
    [[nodiscard]] const Order* find(OrderId id) const;

    Order* best(Side side);
    void popBest(Side side);

    [[nodiscard]] std::optional<Price> bestBid() const;
    [[nodiscard]] std::optional<Price> bestAsk() const;
    [[nodiscard]] std::size_t orderCount() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t levelCount(Side side) const noexcept;
    [[nodiscard]] Quantity depthAt(Side side, Price price) const;
    void reserve(std::size_t n) { index_.reserve(n); }

    // Visits resting orders on one side, best price first, FIFO within a level.
    template <typename F>
    void forEachOrder(Side side, F&& fn) const {
        auto visit = [&](const auto& levels) {
            for (const auto& [price, queue] : levels)
                for (const Order& o : queue) fn(o);
        };
        side == Side::Buy ? visit(bids_) : visit(asks_);
    }

private:
    using Queue = std::list<Order>;
    struct Locator {
        Side side;
        Queue::iterator it;
    };

    std::map<Price, Queue, std::greater<>> bids_;
    std::map<Price, Queue, std::less<>> asks_;
    std::unordered_map<OrderId, Locator> index_;

    template <typename Levels>
    void eraseFrom(Levels& levels, Price price, Queue::iterator it);
};

static_assert(OrderBookLike<MapListOrderBook>);

}  // namespace limitbook
