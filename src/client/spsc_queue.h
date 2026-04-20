#pragma once
#include <array>
#include <cstddef>
#include <bit>
#include <atomic>

template<typename data_T, std::size_t size_T>
class SpscQueue {
public:
    static_assert(size_T >= 2, "Queue size must be at least 2");
    static_assert(std::popcount(size_T) == 1, "Size of the queue must be power of 2");
    bool tryPush(data_T element) {
        auto head = head_.load(std::memory_order_acquire);
        auto tail = tail_.load(std::memory_order_relaxed);
        if (head - tail == size_T) {
            return false;
        }
        buffer_[tail & m_mask] = element;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }
    bool tryPop(data_T& element) {
        auto head = head_.load(std::memory_order_relaxed);
        auto tail = tail_.load(std::memory_order_acquire);
        if (head == tail) {
            return false;
        }
        element = buffer_[head & m_mask];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }


private:
    std::size_t m_mask = size_T - 1;
    std::atomic<std::size_t> head_ = 0;
    std::atomic<std::size_t> tail_ = 0;
    std::array<data_T, size_T> buffer_;
};