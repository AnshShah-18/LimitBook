#include "pooled_order_book.hpp"


namespace limitbook {

// Linear scan from the best price (back of the vector) toward worse prices.
// Profiling showed std::lower_bound at ~20% of engine instructions: a binary search
// always pays log2(L) unpredictable branches, but almost all activity lands within a
// few ticks of the touch, so scanning from the back usually stops after 1-3 levels.
// Worst case is O(L) for a far-from-touch price, the same order as the vector shift
// that inserting or erasing such a level costs anyway.
std::size_t PooledOrderBook::lowerBound(const Levels& lv, Side side, Price price) noexcept {
    std::size_t i = lv.size();
    if (side == Side::Buy) {  // ascending: skip levels priced at or above `price`
        while (i > 0 && lv[i - 1].price >= price) --i;
    } else {                  // descending: skip levels priced at or below `price`
        while (i > 0 && lv[i - 1].price <= price) --i;
    }
    return i;
}

std::uint32_t PooledOrderBook::allocNode(const Order& o) {
    std::uint32_t n;
    if (!free_.empty()) {
        n = free_.back();
        free_.pop_back();
        nodes_[n] = Node{o};
    } else {
        n = static_cast<std::uint32_t>(nodes_.size());
        nodes_.push_back(Node{o});
    }
    return n;
}

void PooledOrderBook::freeNode(std::uint32_t n) { free_.push_back(n); }

void PooledOrderBook::reserve(std::size_t n) {
    nodes_.reserve(n);
    free_.reserve(n);
    index_.reserve(n);
}

bool PooledOrderBook::add(const Order& order) {
    if (index_.find(order.id) != FlatIdMap::kNone) return false;
    const std::uint32_t n = allocNode(order);
    index_.insert(order.id, n);

    Levels& lv = levels(order.side);
    const std::size_t i = lowerBound(lv, order.side, order.price);
    if (i < lv.size() && lv[i].price == order.price) {
        Level& level = lv[i];
        nodes_[n].prev = level.tail;
        nodes_[level.tail].next = n;
        level.tail = n;
    } else {
        lv.insert(lv.begin() + static_cast<std::ptrdiff_t>(i), Level{order.price, n, n});
    }
    return true;
}

void PooledOrderBook::unlink(Levels& lv, std::size_t levelIdx, std::uint32_t n) {
    Level& level = lv[levelIdx];
    Node& node = nodes_[n];
    if (node.prev != kNil) nodes_[node.prev].next = node.next;
    else level.head = node.next;
    if (node.next != kNil) nodes_[node.next].prev = node.prev;
    else level.tail = node.prev;
    if (level.head == kNil) lv.erase(lv.begin() + static_cast<std::ptrdiff_t>(levelIdx));
    freeNode(n);
}

bool PooledOrderBook::cancel(OrderId id) {
    const std::uint32_t n = index_.find(id);
    if (n == FlatIdMap::kNone) return false;
    index_.erase(id);
    const Order& o = nodes_[n].order;
    Levels& lv = levels(o.side);
    unlink(lv, lowerBound(lv, o.side, o.price), n);
    return true;
}

bool PooledOrderBook::reduceTo(OrderId id, Quantity newRemaining) {
    const std::uint32_t n = index_.find(id);
    if (n == FlatIdMap::kNone) return false;
    Order& o = nodes_[n].order;
    if (newRemaining == 0 || newRemaining > o.remaining) return false;
    o.quantity -= o.remaining - newRemaining;
    o.remaining = newRemaining;
    return true;
}

const Order* PooledOrderBook::find(OrderId id) const {
    const std::uint32_t n = index_.find(id);
    return n == FlatIdMap::kNone ? nullptr : &nodes_[n].order;
}

Order* PooledOrderBook::best(Side side) {
    Levels& lv = levels(side);
    return lv.empty() ? nullptr : &nodes_[lv.back().head].order;
}

void PooledOrderBook::popBest(Side side) {
    Levels& lv = levels(side);
    const std::uint32_t n = lv.back().head;
    index_.erase(nodes_[n].order.id);
    unlink(lv, lv.size() - 1, n);
}

std::optional<Price> PooledOrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.back().price;
}

std::optional<Price> PooledOrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.back().price;
}

Quantity PooledOrderBook::depthAt(Side side, Price price) const {
    const Levels& lv = levels(side);
    const std::size_t i = lowerBound(lv, side, price);
    Quantity total = 0;
    if (i < lv.size() && lv[i].price == price)
        for (std::uint32_t n = lv[i].head; n != kNil; n = nodes_[n].next)
            total += nodes_[n].order.remaining;
    return total;
}

}  // namespace limitbook
