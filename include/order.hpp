#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace limitbook {

// Prices are stored as integer ticks (cents): 101.25 -> 10125.
// Integer arithmetic keeps price comparisons exact and keys stable in maps.
using OrderId  = std::uint64_t;
using Price    = std::int64_t;
using Quantity = std::uint32_t;
using SeqNum   = std::uint64_t;

enum class Side : std::uint8_t { Buy, Sell };

constexpr Side opposite(Side s) noexcept { return s == Side::Buy ? Side::Sell : Side::Buy; }
std::string_view toString(Side s) noexcept;

// Converts a decimal price such as "101.25" to ticks. Returns false on bad input.
bool parsePrice(std::string_view text, Price& out) noexcept;
std::string formatPrice(Price ticks);

struct Order {
    OrderId  id{};
    Side     side{Side::Buy};
    Price    price{};
    Quantity quantity{};   // original quantity (or quantity after a modify)
    Quantity remaining{};  // still open; quantity - remaining = filled
    SeqNum   seq{};        // arrival sequence: lower = older = higher time priority

    [[nodiscard]] Quantity filled() const noexcept { return quantity - remaining; }
    [[nodiscard]] bool isFilled() const noexcept { return remaining == 0; }

    // Fills up to `qty`; never lets remaining underflow. Returns the amount filled.
    Quantity fill(Quantity qty) noexcept {
        const Quantity done = qty < remaining ? qty : remaining;
        remaining -= done;
        return done;
    }
};

struct Trade {
    std::uint64_t tradeId{};
    OrderId       buyOrderId{};
    OrderId       sellOrderId{};
    Price         price{};     // always the resting (maker) order's price
    Quantity      quantity{};
    SeqNum        seq{};       // engine sequence number of the event that caused it
    bool operator==(const Trade&) const = default;
};

}  // namespace limitbook
