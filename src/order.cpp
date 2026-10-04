#include "order.hpp"

#include <cstdio>

namespace limitbook {

std::string_view toString(Side s) noexcept { return s == Side::Buy ? "BUY" : "SELL"; }

bool parsePrice(std::string_view text, Price& out) noexcept {
    if (text.empty()) return false;
    Price whole = 0, frac = 0;
    int fracDigits = 0;
    bool seenDot = false, seenDigit = false;
    for (char c : text) {
        if (c == '.') {
            if (seenDot) return false;
            seenDot = true;
        } else if (c >= '0' && c <= '9') {
            seenDigit = true;
            if (!seenDot) {
                whole = whole * 10 + (c - '0');
            } else {
                if (++fracDigits > 2) return false;  // sub-cent prices are not supported
                frac = frac * 10 + (c - '0');
            }
        } else {
            return false;
        }
    }
    if (!seenDigit) return false;
    if (fracDigits == 1) frac *= 10;
    out = whole * 100 + frac;
    return true;
}

std::string formatPrice(Price ticks) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%lld.%02lld", static_cast<long long>(ticks / 100),
                  static_cast<long long>(ticks % 100));
    return buf;
}

}  // namespace limitbook
