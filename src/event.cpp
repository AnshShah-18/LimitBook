#include "event.hpp"

#include <charconv>
#include <vector>

namespace aquila {

std::string toString(const Event& e) {
    switch (e.type) {
        case EventType::Add:
            return "ADD " + std::string(toString(e.side)) + ' ' + std::to_string(e.id) + ' ' +
                   std::to_string(e.price) + ' ' + std::to_string(e.quantity);
        case EventType::Cancel:
            return "CANCEL " + std::to_string(e.id);
        case EventType::Modify:
            return "MODIFY " + std::to_string(e.id) + ' ' + std::to_string(e.price) + ' ' +
                   std::to_string(e.quantity);
    }
    return {};
}

namespace {

std::vector<std::string_view> split(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ' ') ++i;
        std::size_t j = i;
        while (j < line.size() && line[j] != ' ') ++j;
        if (j > i) out.push_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

template <typename T>
bool toNumber(std::string_view s, T& out) {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

}  // namespace

std::optional<Event> parseEvent(std::string_view line) {
    const auto t = split(line);
    if (t.empty()) return std::nullopt;
    Event e;
    if (t[0] == "ADD" && t.size() == 5) {
        e.type = EventType::Add;
        if (t[1] == "BUY") e.side = Side::Buy;
        else if (t[1] == "SELL") e.side = Side::Sell;
        else return std::nullopt;
        if (toNumber(t[2], e.id) && toNumber(t[3], e.price) && toNumber(t[4], e.quantity))
            return e;
    } else if (t[0] == "CANCEL" && t.size() == 2) {
        e.type = EventType::Cancel;
        if (toNumber(t[1], e.id)) return e;
    } else if (t[0] == "MODIFY" && t.size() == 4) {
        e.type = EventType::Modify;
        if (toNumber(t[1], e.id) && toNumber(t[2], e.price) && toNumber(t[3], e.quantity))
            return e;
    }
    return std::nullopt;
}

}  // namespace aquila
