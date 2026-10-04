#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "flat_id_map.hpp"
#include "order_book.hpp"

namespace limitbook {

// Optimized order book (Phase 8). Same contract and behaviour as MapListOrderBook;
// the difference is purely memory layout. Profiling the baseline showed roughly a
// third of engine instructions inside malloc/free (list, hash and tree nodes).
// This version performs no heap allocation per order in steady state:
//
//   order storage : one std::vector<Node> pool + free list; nodes are addressed by
//                   32-bit index, so the pool can grow without invalidating links
//   FIFO queue    : intrusive doubly linked list threaded through the pool nodes
//   price levels  : per side, a std::vector<Level> sorted so the BEST price is at
//                   back(). Most activity is near the touch, so inserting/erasing a
//                   level shifts only a few elements, and top-of-book is back().
//   id lookup     : FlatIdMap (open addressing, backward-shift delete)
//
// Trade-off: a level far from the touch costs O(L) to insert/erase (vector shift),
// where L is the number of price levels on that side. For realistic books L is small
// (tens to low hundreds); std::map's O(log L) would win only for very deep books.
class PooledOrderBook {
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
    [[nodiscard]] std::size_t levelCount(Side side) const noexcept { return levels(side).size(); }
    [[nodiscard]] Quantity depthAt(Side side, Price price) const;
    void reserve(std::size_t n);

    template <typename F>
    void forEachOrder(Side side, F&& fn) const {
        const auto& lv = levels(side);
        for (auto it = lv.rbegin(); it != lv.rend(); ++it)
            for (std::uint32_t n = it->head; n != kNil; n = nodes_[n].next) fn(nodes_[n].order);
    }

private:
    static constexpr std::uint32_t kNil = FlatIdMap::kNone;

    struct Node {
        Order order;
        std::uint32_t prev = kNil;
        std::uint32_t next = kNil;
    };
    struct Level {
        Price price;
        std::uint32_t head = kNil;
        std::uint32_t tail = kNil;
    };
    using Levels = std::vector<Level>;

    Levels& levels(Side s) noexcept { return s == Side::Buy ? bids_ : asks_; }
    const Levels& levels(Side s) const noexcept { return s == Side::Buy ? bids_ : asks_; }

    // Index of the level with `price`, or of the position where it would be inserted.
    static std::size_t lowerBound(const Levels& lv, Side side, Price price) noexcept;

    std::uint32_t allocNode(const Order& o);
    void freeNode(std::uint32_t n);
    void unlink(Levels& lv, std::size_t levelIdx, std::uint32_t n);

    std::vector<Node> nodes_;
    std::vector<std::uint32_t> free_;
    Levels bids_;  // ascending price: best (highest) bid at back()
    Levels asks_;  // descending price: best (lowest) ask at back()
    FlatIdMap index_;
};

static_assert(OrderBookLike<PooledOrderBook>);

}  // namespace limitbook
