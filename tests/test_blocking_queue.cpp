#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "blocking_queue.hpp"

using namespace std::chrono_literals;

namespace {

TEST(BlockingQueue, FifoOrderOnUnboundedQueue) {
    palim::BlockingQueue<int> q(0);
    q.push(1);
    q.push(2);
    q.push(3);
    EXPECT_EQ(*q.waitAndPop(), 1);
    EXPECT_EQ(*q.waitAndPop(), 2);
    EXPECT_EQ(*q.waitAndPop(), 3);
}

TEST(BlockingQueue, PushPastCapacityDropsOldest) {
    palim::BlockingQueue<int> q(3);
    q.push(1);
    q.push(2);
    q.push(3);
    q.push(4);  // evicts 1
    q.push(5);  // evicts 2
    EXPECT_EQ(*q.waitAndPop(), 3);
    EXPECT_EQ(*q.waitAndPop(), 4);
    EXPECT_EQ(*q.waitAndPop(), 5);
}

TEST(BlockingQueue, ConsumerBlocksUntilPush) {
    palim::BlockingQueue<int> q(0);
    std::atomic<bool> gotItem{false};

    std::thread consumer([&] {
        auto item = q.waitAndPop();
        ASSERT_TRUE(item.has_value());
        EXPECT_EQ(*item, 42);
        gotItem.store(true);
    });

    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(gotItem.load());  // must still be blocked, nothing pushed yet

    q.push(42);
    consumer.join();
    EXPECT_TRUE(gotItem.load());
}

TEST(BlockingQueue, ShutdownUnblocksWaitingConsumerWithNullopt) {
    palim::BlockingQueue<int> q(0);
    std::atomic<bool> returned{false};
    std::optional<int> result{123};  // sentinel != nullopt

    std::thread consumer([&] {
        result = q.waitAndPop();
        returned.store(true);
    });

    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(returned.load());  // still blocked, queue empty, no shutdown yet

    q.shutdown();
    consumer.join();
    EXPECT_TRUE(returned.load());
    EXPECT_FALSE(result.has_value());
}

TEST(BlockingQueue, ShutdownDrainsRemainingItemsBeforeNullopt) {
    palim::BlockingQueue<int> q(0);
    q.push(1);
    q.push(2);
    q.shutdown();  // no more items will ever be pushed after this

    EXPECT_EQ(*q.waitAndPop(), 1);
    EXPECT_EQ(*q.waitAndPop(), 2);
    EXPECT_FALSE(q.waitAndPop().has_value());
}

TEST(BlockingQueue, ConcurrentProducerConsumerLosesNothingWhenUnbounded) {
    palim::BlockingQueue<int> q(0);
    constexpr int kCount = 5000;
    std::vector<int> received;
    received.reserve(kCount);

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            q.push(i);
        }
        q.shutdown();
    });

    std::thread consumer([&] {
        while (auto item = q.waitAndPop()) {
            received.push_back(*item);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(received.size()), kCount);
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(received[static_cast<std::size_t>(i)], i);
    }
}

TEST(BlockingQueue, BoundedQueueUnderLoadStaysOrderedAndKeepsTheLastItem) {
    palim::BlockingQueue<int> q(4);
    constexpr int kCount = 2000;
    std::vector<int> received;

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            q.push(i);
        }
        q.shutdown();
    });

    std::thread consumer([&] {
        while (auto item = q.waitAndPop()) {
            received.push_back(*item);
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    producer.join();
    consumer.join();

    ASSERT_FALSE(received.empty());
    for (std::size_t i = 1; i < received.size(); ++i) {
        EXPECT_GT(received[i], received[i - 1]);  // strictly increasing: no dupes, no reordering
    }
    EXPECT_EQ(received.back(), kCount - 1);  // the very last pushed item is never dropped
}

}  // namespace
