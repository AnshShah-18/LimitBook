#pragma once

#include <atomic>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

namespace aquila {

// Interface shared by the queue implementations. The pipeline is templated on it,
// so swapping the mutex queue for the lock-free ring is a one-word change.
//   push(item)  blocks while the queue is full
//   pop(item)   blocks while empty; returns false once closed and drained
//   close()     producer signals end of stream
template <typename Q, typename T>
concept BlockingQueue = requires(Q q, T item, T& out) {
    { q.push(std::move(item)) } -> std::same_as<void>;
    { q.pop(out) } -> std::same_as<bool>;
    { q.close() } -> std::same_as<void>;
};

// Bounded MPMC queue built from std::mutex + two condition variables.
// Simple, obviously correct, and the baseline for the concurrency experiment.
template <typename T>
class MutexQueue {
public:
    explicit MutexQueue(std::size_t capacity = 65'536) : capacity_(capacity) {}

    void push(T item) {
        std::unique_lock lock(mutex_);
        notFull_.wait(lock, [&] { return items_.size() < capacity_; });
        items_.push_back(std::move(item));
        lock.unlock();
        notEmpty_.notify_one();
    }

    bool pop(T& out) {
        std::unique_lock lock(mutex_);
        notEmpty_.wait(lock, [&] { return !items_.empty() || closed_; });
        if (items_.empty()) return false;  // closed and drained
        out = std::move(items_.front());
        items_.pop_front();
        lock.unlock();
        notFull_.notify_one();
        return true;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        notEmpty_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<T> items_;
    std::size_t capacity_;
    bool closed_ = false;
};

// Single-producer / single-consumer ring buffer using acquire/release atomics.
//  * Exactly one thread may push and exactly one thread may pop.
//  * head_ (consumer-owned) and tail_ (producer-owned) live on separate cache lines
//    so the two cores do not false-share; each side also caches the other's index
//    and only re-reads the shared atomic when the cached value says full/empty.
//  * Capacity is rounded up to a power of two so wrap-around is a mask, not a modulo.
//  * Blocking is a spin that yields; fine for a dedicated core, wasteful otherwise.
template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(std::size_t capacity = 65'536)
        : mask_(roundUp(capacity) - 1), slots_(std::make_unique<T[]>(mask_ + 1)) {}

    void push(T item) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        while (tail - cachedHead_ > mask_) {  // looks full: refresh our view of head
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (tail - cachedHead_ > mask_) std::this_thread::yield();
        }
        slots_[tail & mask_] = std::move(item);
        tail_.store(tail + 1, std::memory_order_release);  // publish the slot
    }

    bool pop(T& out) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        while (head == cachedTail_) {  // looks empty: refresh our view of tail
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (head != cachedTail_) break;
            if (closed_.load(std::memory_order_acquire)) {
                // Re-check: items pushed before close() must still be delivered.
                cachedTail_ = tail_.load(std::memory_order_acquire);
                if (head == cachedTail_) return false;
                break;
            }
            std::this_thread::yield();
        }
        out = std::move(slots_[head & mask_]);
        head_.store(head + 1, std::memory_order_release);  // free the slot
        return true;
    }

    void close() { closed_.store(true, std::memory_order_release); }

private:
    static std::size_t roundUp(std::size_t n) {
        std::size_t p = 2;
        while (p < n) p <<= 1;
        return p;
    }

    static constexpr std::size_t kCacheLine = 64;

    const std::size_t mask_;
    std::unique_ptr<T[]> slots_;
    alignas(kCacheLine) std::atomic<std::size_t> head_{0};  // written by consumer
    std::size_t cachedTail_ = 0;                             // consumer's copy of tail
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};  // written by producer
    std::size_t cachedHead_ = 0;                             // producer's copy of head
    alignas(kCacheLine) std::atomic<bool> closed_{false};
};

}  // namespace aquila
