#pragma once

#include <cstdint>
#include <limits>
#include <vector>

#include "order.hpp"

namespace limitbook {

// Open-addressing hash map OrderId -> uint32 slot index, used by PooledOrderBook in
// place of std::unordered_map.
//  * One contiguous array: no per-entry heap node, lookups touch one or two cache lines.
//  * Linear probing with Fibonacci hashing (multiply by 2^64/phi, keep the top bits),
//    which spreads sequential ids well.
//  * Deletion uses backward-shift instead of tombstones, so probe chains never decay
//    under the heavy add/cancel churn of an order book.
//  * Grows (and rehashes) when the load factor would exceed 1/2.
class FlatIdMap {
public:
    static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

    FlatIdMap() { rehash(16); }

    void reserve(std::size_t n) {
        std::size_t want = 16;
        while (want < n * 2) want <<= 1;
        if (want > slots_.size()) rehash(want);
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

    [[nodiscard]] std::uint32_t find(OrderId key) const noexcept {
        for (std::size_t i = home(key);; i = (i + 1) & mask_) {
            const Slot& s = slots_[i];
            if (s.value == kNone) return kNone;
            if (s.key == key) return s.value;
        }
    }

    // Returns false (and changes nothing) if the key is already present.
    bool insert(OrderId key, std::uint32_t value) {
        if ((size_ + 1) * 2 > slots_.size()) rehash(slots_.size() * 2);
        for (std::size_t i = home(key);; i = (i + 1) & mask_) {
            Slot& s = slots_[i];
            if (s.value == kNone) {
                s = {key, value};
                ++size_;
                return true;
            }
            if (s.key == key) return false;
        }
    }

    bool erase(OrderId key) noexcept {
        std::size_t i = home(key);
        for (;; i = (i + 1) & mask_) {
            if (slots_[i].value == kNone) return false;
            if (slots_[i].key == key) break;
        }
        // Backward-shift: pull later entries of the probe chain into the hole when
        // their home slot is at or before the hole (cyclically).
        for (std::size_t j = (i + 1) & mask_;; j = (j + 1) & mask_) {
            if (slots_[j].value == kNone) break;
            const std::size_t k = home(slots_[j].key);
            if (((j - k) & mask_) >= ((j - i) & mask_)) {
                slots_[i] = slots_[j];
                i = j;
            }
        }
        slots_[i].value = kNone;
        --size_;
        return true;
    }

private:
    struct Slot {
        OrderId key = 0;
        std::uint32_t value = kNone;  // kNone marks an empty slot
    };

    [[nodiscard]] std::size_t home(OrderId key) const noexcept {
        return static_cast<std::size_t>((key * 0x9E3779B97F4A7C15ull) >> shift_);
    }

    void rehash(std::size_t capacity) {
        std::vector<Slot> old = std::move(slots_);
        slots_.assign(capacity, Slot{});
        mask_ = capacity - 1;
        shift_ = 64;
        for (std::size_t c = capacity; c > 1; c >>= 1) --shift_;
        size_ = 0;
        for (const Slot& s : old)
            if (s.value != kNone) insert(s.key, s.value);
    }

    std::vector<Slot> slots_;
    std::size_t mask_ = 0;
    unsigned shift_ = 64;
    std::size_t size_ = 0;
};

}  // namespace limitbook
