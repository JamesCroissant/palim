#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace palim {

// Fixed-capacity circular buffer. Pushing past capacity overwrites the
// oldest element. Written by hand (not std::deque) so the wraparound
// indexing stays visible instead of hidden behind a container.
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t capacity) : buffer_(capacity), capacity_(capacity) {}

    void push(T item) {
        buffer_[writeIndex_] = std::move(item);
        writeIndex_ = (writeIndex_ + 1) % capacity_;
        if (count_ < capacity_) {
            ++count_;
        }
    }

    // Oldest-to-newest copy of everything currently stored.
    std::vector<T> snapshot() const {
        std::vector<T> result;
        result.reserve(count_);
        const std::size_t oldestIndex = (count_ < capacity_) ? 0 : writeIndex_;
        for (std::size_t i = 0; i < count_; ++i) {
            result.push_back(buffer_[(oldestIndex + i) % capacity_]);
        }
        return result;
    }

    std::size_t size() const { return count_; }
    std::size_t capacity() const { return capacity_; }

private:
    std::vector<T> buffer_;
    std::size_t capacity_;
    std::size_t writeIndex_ = 0;
    std::size_t count_ = 0;
};

}  // namespace palim
