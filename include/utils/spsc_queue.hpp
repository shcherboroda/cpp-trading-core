#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace utils {

template <typename T>
class SpscQueue {
public:
    explicit SpscQueue(std::size_t capacity)
        : capacity_(capacity),
          buffer_(capacity),
          head_(0),
          tail_(0)
    {}

    // single-producer
    bool push(const T& value) {
        auto head = head_.load(std::memory_order_relaxed);
        auto next = increment(head);

        // очередь полна, если следующий head догоняет tail
        if (next == tail_.load(std::memory_order_acquire)) {
            return false; // full
        }

        buffer_[head] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // single-consumer
    bool pop(T& out) {
        auto tail = tail_.load(std::memory_order_relaxed);

        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // empty
        }

        out = buffer_[tail];
        tail_.store(increment(tail), std::memory_order_release);
        return true;
    }

    bool empty() const {
        auto tail = tail_.load(std::memory_order_relaxed);
        auto head = head_.load(std::memory_order_relaxed);
        return tail == head;
    }

    bool full() const {
        auto head = head_.load(std::memory_order_relaxed);
        auto next = increment(head);
        auto tail = tail_.load(std::memory_order_relaxed);
        return next == tail;
    }

    std::size_t capacity() const { return capacity_; }

private:
    std::size_t increment(std::size_t idx) const noexcept {
        ++idx;
        if (idx == capacity_) idx = 0;
        return idx;
    }

    const std::size_t         capacity_;
    std::vector<T>            buffer_;
    std::atomic<std::size_t>  head_;
    std::atomic<std::size_t>  tail_;
};

} // namespace utils
