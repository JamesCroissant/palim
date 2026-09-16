#include <gtest/gtest.h>

#include "ring_buffer.hpp"

namespace {

TEST(RingBuffer, HoldsItemsInOrderBeforeFull) {
    palim::RingBuffer<int> rb(3);
    rb.push(1);
    rb.push(2);
    rb.push(3);
    EXPECT_EQ(rb.snapshot(), (std::vector<int>{1, 2, 3}));
}

TEST(RingBuffer, PushPastCapacityEvictsOldest) {
    palim::RingBuffer<int> rb(3);
    rb.push(1);
    rb.push(2);
    rb.push(3);
    rb.push(4);  // evicts 1
    EXPECT_EQ(rb.snapshot(), (std::vector<int>{2, 3, 4}));

    rb.push(5);
    rb.push(6);
    EXPECT_EQ(rb.snapshot(), (std::vector<int>{4, 5, 6}));
}

TEST(RingBuffer, SizeAndCapacityReportCorrectly) {
    palim::RingBuffer<int> rb(3);
    EXPECT_EQ(rb.capacity(), 3u);
    EXPECT_EQ(rb.size(), 0u);

    rb.push(1);
    EXPECT_EQ(rb.size(), 1u);

    rb.push(2);
    rb.push(3);
    rb.push(4);  // at capacity, evicting -- size stays at capacity
    EXPECT_EQ(rb.size(), 3u);
}

}  // namespace
