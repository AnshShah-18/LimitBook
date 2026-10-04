#include "order_book.hpp"

namespace aquila {

bool MapListOrderBook::add(const Order& order) {
    if (index_.contains(order.id)) return false;
    Queue* queue = order.side == Side::Buy ? &bids_[order.price] : &asks_[order.price];
    queue->push_back(order);
    index_.emplace(order.id, Locator{order.side, std::prev(queue->end())});
    return true;
}

template <typename Levels>
void MapListOrderBook::eraseFrom(Levels& levels, Price price, Queue::iterator it) {
    auto level = levels.find(price);
    level->second.erase(it);
    if (level->second.empty()) levels.erase(level);
}

bool MapListOrderBook::cancel(OrderId id) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;
    const auto [side, it] = found->second;
    const Price price = it->price;
    index_.erase(found);
    if (side == Side::Buy) eraseFrom(bids_, price, it);
    else eraseFrom(asks_, price, it);
    return true;
}

bool MapListOrderBook::reduceTo(OrderId id, Quantity newRemaining) {
    auto found = index_.find(id);
    if (found == index_.end()) return false;
    Order& o = *found->second.it;
    if (newRemaining == 0 || newRemaining > o.remaining) return false;
    o.quantity -= o.remaining - newRemaining;  // keep filled() unchanged
    o.remaining = newRemaining;
    return true;
}

const Order* MapListOrderBook::find(OrderId id) const {
    auto found = index_.find(id);
    return found == index_.end() ? nullptr : &*found->second.it;
}

Order* MapListOrderBook::best(Side side) {
    if (side == Side::Buy) return bids_.empty() ? nullptr : &bids_.begin()->second.front();
    return asks_.empty() ? nullptr : &asks_.begin()->second.front();
}

void MapListOrderBook::popBest(Side side) {
    auto pop = [this](auto& levels) {
        auto level = levels.begin();
        index_.erase(level->second.front().id);
        level->second.pop_front();
        if (level->second.empty()) levels.erase(level);
    };
    side == Side::Buy ? pop(bids_) : pop(asks_);
}

std::optional<Price> MapListOrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<Price> MapListOrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::size_t MapListOrderBook::levelCount(Side side) const noexcept {
    return side == Side::Buy ? bids_.size() : asks_.size();
}

Quantity MapListOrderBook::depthAt(Side side, Price price) const {
    auto sum = [price](const auto& levels) {
        Quantity total = 0;
        if (auto it = levels.find(price); it != levels.end())
            for (const Order& o : it->second) total += o.remaining;
        return total;
    };
    return side == Side::Buy ? sum(bids_) : sum(asks_);
}

}  // namespace aquila
