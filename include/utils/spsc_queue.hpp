#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace utils {

#ifndef UTILS_SPSC_QUEUE_PAD_INDICES
#define UTILS_SPSC_QUEUE_PAD_INDICES 0
#endif

#ifndef UTILS_SPSC_QUEUE_MOVE_TRANSFER
#define UTILS_SPSC_QUEUE_MOVE_TRANSFER 0
#endif

#ifndef UTILS_SPSC_QUEUE_MASK_INCREMENT
#define UTILS_SPSC_QUEUE_MASK_INCREMENT 0
#endif

inline constexpr std::size_t kSpscQueueIndexAlignment = 64;

template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity)
        : capacity_(capacity),
          buffer_(capacity),
          head_(),
          tail_()
    {
#if UTILS_SPSC_QUEUE_MASK_INCREMENT
        if (capacity_ < 2 || (capacity_ & (capacity_ - 1)) != 0) {
            throw std::invalid_argument("masked SPSC queue capacity must be a power of two");
        }
#endif
    }

    // single-producer
    bool push(const T& value) {
        return push_impl(value);
    }

    // single-producer
    bool push(T&& value) {
        return push_impl(std::move(value));
    }

    // single-consumer
    bool pop(T& out) {
        auto tail = tail_.value.load(std::memory_order_relaxed);

        if (tail == head_.value.load(std::memory_order_acquire)) {
            return false; // empty
        }

#if UTILS_SPSC_QUEUE_MOVE_TRANSFER
        out = std::move(buffer_[tail]);
#else
        out = buffer_[tail];
#endif
        tail_.value.store(increment(tail), std::memory_order_release);
        return true;
    }

    bool empty() const {
        auto tail = tail_.value.load(std::memory_order_relaxed);
        auto head = head_.value.load(std::memory_order_relaxed);
        return tail == head;
    }

    bool full() const {
        auto head = head_.value.load(std::memory_order_relaxed);
        auto next = increment(head);
        auto tail = tail_.value.load(std::memory_order_relaxed);
        return next == tail;
    }

    std::size_t capacity() const { return capacity_; }

private:
    template <typename U>
    bool push_impl(U&& value) {
        auto head = head_.value.load(std::memory_order_relaxed);
        auto next = increment(head);

        // очередь полна, если следующий head догоняет tail
        if (next == tail_.value.load(std::memory_order_acquire)) {
            return false; // full
        }

        buffer_[head] = std::forward<U>(value);
        head_.value.store(next, std::memory_order_release);
        return true;
    }
#if UTILS_SPSC_QUEUE_PAD_INDICES
    struct alignas(kSpscQueueIndexAlignment) Index {
#else
    struct Index {
#endif
        std::atomic<std::size_t> value{0};
    };

#if UTILS_SPSC_QUEUE_PAD_INDICES
    static_assert(alignof(Index) >= kSpscQueueIndexAlignment);
#endif

    std::size_t increment(std::size_t idx) const noexcept {
#if UTILS_SPSC_QUEUE_MASK_INCREMENT
        return (idx + 1) & (capacity_ - 1);
#else
        ++idx;
        if (idx == capacity_) idx = 0;
        return idx;
#endif
    }

    const std::size_t         capacity_;
    std::vector<T>            buffer_;
    // With padding enabled, producer and consumer update distinct cache lines.
    Index                     head_;
    Index                     tail_;
};

} // namespace utils
