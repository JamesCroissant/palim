#include <gtest/gtest.h>

#include "consecutive_failure_guard.hpp"

namespace {

TEST(ConsecutiveFailureGuard, DoesNotGiveUpBeforeThreshold) {
    palim::ConsecutiveFailureGuard guard(3);
    EXPECT_FALSE(guard.recordFailure());
    EXPECT_FALSE(guard.recordFailure());
}

TEST(ConsecutiveFailureGuard, GivesUpOnceThresholdReached) {
    palim::ConsecutiveFailureGuard guard(3);
    EXPECT_FALSE(guard.recordFailure());
    EXPECT_FALSE(guard.recordFailure());
    EXPECT_TRUE(guard.recordFailure());
}

// A single failed attempt is normal and expected to recover -- the
// whole point of this class is to distinguish that from a permanent
// failure, so a success in between must reset the count rather than
// letting failures accumulate across it.
TEST(ConsecutiveFailureGuard, SuccessResetsTheCount) {
    palim::ConsecutiveFailureGuard guard(3);
    guard.recordFailure();
    guard.recordFailure();
    guard.recordSuccess();
    EXPECT_FALSE(guard.recordFailure());
    EXPECT_FALSE(guard.recordFailure());
    EXPECT_TRUE(guard.recordFailure());
}

TEST(ConsecutiveFailureGuard, ThresholdOfOneGivesUpOnTheFirstFailure) {
    palim::ConsecutiveFailureGuard guard(1);
    EXPECT_TRUE(guard.recordFailure());
}

// Once given up, it stays given up on further failures (main.cpp's
// capture thread breaks out of its loop right away, but nothing about
// the guard itself should silently stop reporting true if it were ever
// called again).
TEST(ConsecutiveFailureGuard, StaysGivenUpOnFurtherFailures) {
    palim::ConsecutiveFailureGuard guard(2);
    EXPECT_FALSE(guard.recordFailure());
    EXPECT_TRUE(guard.recordFailure());
    EXPECT_TRUE(guard.recordFailure());
}

}  // namespace
