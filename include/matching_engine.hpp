#pragma once

#include <cstdint>
#include <vector>

#include "event.hpp"
#include "order_book.hpp"
#include "pooled_order_book.hpp"

namespace aquila {

enum class Status : std::uint8_t {
    Accepted,           // add/modify accepted (may have traded, may be resting)
    Cancelled,
    RejectedDuplicate,  // an order with this id is already resting
    RejectedInvalid,    // zero quantity or non-positive price
    NotFound,           // cancel/modify for an id that is not resting (unknown, filled, cancelled)
};

struct EngineStats {
    std::uint64_t events = 0;
    std::uint64_t trades = 0;
    std::uint64_t volume = 0;  // total shares executed
    std::uint64_t rejected = 0;
    std::uint64_t notFound = 0;
};

// Price-time priority matching on top of any OrderBookLike container.
//
// Rules:
//  * An incoming order matches against the opposite side while it crosses
//    (buy price >= best ask, or sell price <= best bid).
//  * Executions happen at the resting order's price (the maker sets the price).
//  * Within a level, the oldest resting order fills first.
//  * Any unfilled remainder rests in the book with the event's sequence number.
//  * Modify: lowering quantity at the same price keeps time priority. Changing the
//    price or increasing the quantity is cancel + re-add: the order goes to the back
//    of the queue and may trade immediately if the new price crosses.
template <OrderBookLike Book>
class BasicMatchingEngine {
public:
    using BookType = Book;

    Status add(OrderId id, Side side, Price price, Quantity qty, std::vector<Trade>& out) {
        ++stats_.events;
        return addImpl(id, side, price, qty, out);
    }

    Status cancel(OrderId id) {
        ++stats_.events;
        ++seq_;
        if (book_.cancel(id)) return Status::Cancelled;
        ++stats_.notFound;
        return Status::NotFound;
    }

    Status modify(OrderId id, Price newPrice, Quantity newQty, std::vector<Trade>& out) {
        ++stats_.events;
        const Order* existing = book_.find(id);
        if (!existing) {
            ++seq_;
            ++stats_.notFound;
            return Status::NotFound;
        }
        if (newQty == 0 || newPrice <= 0) {
            ++seq_;
            ++stats_.rejected;
            return Status::RejectedInvalid;
        }
        if (newPrice == existing->price && newQty <= existing->remaining) {
            ++seq_;
            if (newQty < existing->remaining) book_.reduceTo(id, newQty);
            return Status::Accepted;
        }
        const Side side = existing->side;
        book_.cancel(id);
        return addImpl(id, side, newPrice, newQty, out);
    }

    Status process(const Event& e, std::vector<Trade>& out) {
        switch (e.type) {
            case EventType::Add: return add(e.id, e.side, e.price, e.quantity, out);
            case EventType::Cancel: return cancel(e.id);
            case EventType::Modify: return modify(e.id, e.price, e.quantity, out);
        }
        return Status::RejectedInvalid;
    }

    [[nodiscard]] const Book& book() const noexcept { return book_; }
    Book& book() noexcept { return book_; }
    [[nodiscard]] const EngineStats& stats() const noexcept { return stats_; }

private:
    Status addImpl(OrderId id, Side side, Price price, Quantity qty, std::vector<Trade>& out) {
        const SeqNum seq = ++seq_;
        if (qty == 0 || price <= 0) {
            ++stats_.rejected;
            return Status::RejectedInvalid;
        }
        if (book_.find(id)) {
            ++stats_.rejected;
            return Status::RejectedDuplicate;
        }
        Order incoming{id, side, price, qty, qty, seq};
        match(incoming, out);
        if (incoming.remaining > 0) book_.add(incoming);
        return Status::Accepted;
    }

    void match(Order& incoming, std::vector<Trade>& out) {
        const Side makerSide = opposite(incoming.side);
        while (incoming.remaining > 0) {
            Order* maker = book_.best(makerSide);
            if (!maker) break;
            const bool crosses = incoming.side == Side::Buy ? incoming.price >= maker->price
                                                            : incoming.price <= maker->price;
            if (!crosses) break;

            const Quantity qty = maker->fill(incoming.remaining);
            incoming.fill(qty);
            const bool buyIsIncoming = incoming.side == Side::Buy;
            out.push_back(Trade{++tradeId_, buyIsIncoming ? incoming.id : maker->id,
                                buyIsIncoming ? maker->id : incoming.id, maker->price, qty,
                                incoming.seq});
            ++stats_.trades;
            stats_.volume += qty;
            if (maker->isFilled()) book_.popBest(makerSide);
        }
    }

    Book book_;
    EngineStats stats_;
    SeqNum seq_ = 0;
    std::uint64_t tradeId_ = 0;
};

using BaselineEngine = BasicMatchingEngine<MapListOrderBook>;
using PooledEngine = BasicMatchingEngine<PooledOrderBook>;

// Explicit instantiations live in matching_engine.cpp.
extern template class BasicMatchingEngine<MapListOrderBook>;
extern template class BasicMatchingEngine<PooledOrderBook>;

}  // namespace aquila
