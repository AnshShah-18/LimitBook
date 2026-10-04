#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "order.hpp"

namespace limitbook {

enum class EventType : std::uint8_t { Add, Cancel, Modify };

// One market event. Text form (prices in ticks, as in the spec):
//   ADD BUY 10001 10125 200     add order 10001: buy 200 @ 101.25
//   CANCEL 10001
//   MODIFY 10001 10130 150      new price 101.30, new open quantity 150
struct Event {
    EventType type{EventType::Add};
    Side      side{Side::Buy};  // used by Add only
    OrderId   id{};
    Price     price{};          // Add / Modify
    Quantity  quantity{};       // Add / Modify
    bool operator==(const Event&) const = default;
};

std::string toString(const Event& e);
std::optional<Event> parseEvent(std::string_view line);

}  // namespace limitbook
