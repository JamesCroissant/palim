#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace palim {

// Thread-safe queue connecting one producer thread to one consumer
// thread.
//
// push() never blocks: if the queue is already at capacity, the oldest
// queued item is dropped to make room. This matters most for the
// capture -> processing frame queue: the capture thread must never
// stall waiting on processing, since stalling it risks losing frames at
// the camera/driver level instead of just dropping an already-stale one
// here. Pass capacity 0 for an effectively unbounded queue (used for the
// commit queue, where items are rare and none should ever be dropped).
//
// waitAndPop() blocks the consumer until an item is available or
// shutdown() is called; after shutdown it drains whatever remains and
// then returns std::nullopt -- the consumer's cue to exit its loop.
template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(std::size_t capacity) : capacity_(capacity) {}

    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (capacity_ > 0 && queue_.size() >= capacity_) {
                queue_.pop_front();
            }
            queue_.push_back(std::move(item));
        }
        cv_.notify_one();
    }

    std::optional<T> waitAndPop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty() || shuttingDown_; });
        if (queue_.empty()) {
            return std::nullopt;  // shut down and fully drained
        }
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shuttingDown_ = true;
        }
        cv_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<T> queue_;
    std::size_t capacity_;  // 0 means unbounded
    bool shuttingDown_ = false;
};

}  // namespace palim
