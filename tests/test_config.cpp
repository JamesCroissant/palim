#include <gtest/gtest.h>

#include "config.hpp"

namespace {

TEST(ParseArgs, DefaultsWhenNoArgsGiven) {
    const auto config = palim::parseArgs({});
    EXPECT_EQ(config.devicePath, "/dev/video0");
    EXPECT_EQ(config.width, 1280);
    EXPECT_EQ(config.height, 720);
    EXPECT_DOUBLE_EQ(config.changeThresholdPercent, 5.0);
    EXPECT_EQ(config.stableDuration, std::chrono::milliseconds(2000));
    EXPECT_EQ(config.outputDir, "commits");
    EXPECT_EQ(config.frameHistoryCapacity, 90);
}

TEST(ParseArgs, OverridesFrameHistoryCapacity) {
    const auto config = palim::parseArgs({"--frame-history-capacity", "30"});
    EXPECT_EQ(config.frameHistoryCapacity, 30);
}

// Zero (or negative) would reach RingBuffer's wraparound indexing, which
// divides by its capacity -- a modulo-by-zero on the very first frame
// pushed, not just an odd-but-safe setting.
TEST(ParseArgs, NonPositiveFrameHistoryCapacityThrows) {
    EXPECT_THROW(palim::parseArgs({"--frame-history-capacity", "0"}), std::invalid_argument);
    EXPECT_THROW(palim::parseArgs({"--frame-history-capacity", "-30"}), std::invalid_argument);
}

TEST(ParseArgs, OverridesDevicePath) {
    const auto config = palim::parseArgs({"--device", "/dev/video2"});
    EXPECT_EQ(config.devicePath, "/dev/video2");
}

TEST(ParseArgs, OverridesWidthAndHeight) {
    const auto config = palim::parseArgs({"--width", "640", "--height", "480"});
    EXPECT_EQ(config.width, 640);
    EXPECT_EQ(config.height, 480);
}

TEST(ParseArgs, OverridesThreshold) {
    const auto config = palim::parseArgs({"--threshold", "12.5"});
    EXPECT_DOUBLE_EQ(config.changeThresholdPercent, 12.5);
}

TEST(ParseArgs, OverridesStableMs) {
    const auto config = palim::parseArgs({"--stable-ms", "500"});
    EXPECT_EQ(config.stableDuration, std::chrono::milliseconds(500));
}

TEST(ParseArgs, OverridesOutputDir) {
    const auto config = palim::parseArgs({"--output-dir", "/tmp/my-commits"});
    EXPECT_EQ(config.outputDir, "/tmp/my-commits");
}

TEST(ParseArgs, AllFlagsTogether) {
    const auto config = palim::parseArgs({"--device", "/dev/video3", "--width", "320", "--height", "240",
                                           "--threshold", "8", "--stable-ms", "1500", "--output-dir", "out",
                                           "--frame-history-capacity", "45"});
    EXPECT_EQ(config.devicePath, "/dev/video3");
    EXPECT_EQ(config.width, 320);
    EXPECT_EQ(config.height, 240);
    EXPECT_DOUBLE_EQ(config.changeThresholdPercent, 8.0);
    EXPECT_EQ(config.stableDuration, std::chrono::milliseconds(1500));
    EXPECT_EQ(config.outputDir, "out");
    EXPECT_EQ(config.frameHistoryCapacity, 45);
}

TEST(ParseArgs, UnrecognizedFlagThrows) {
    EXPECT_THROW(palim::parseArgs({"--bogus"}), std::invalid_argument);
}

TEST(ParseArgs, FlagMissingValueThrows) {
    EXPECT_THROW(palim::parseArgs({"--device"}), std::invalid_argument);
    EXPECT_THROW(palim::parseArgs({"--width"}), std::invalid_argument);
}

TEST(ParseArgs, NonIntegerWidthThrows) {
    EXPECT_THROW(palim::parseArgs({"--width", "wide"}), std::invalid_argument);
}

TEST(ParseArgs, TrailingGarbageAfterNumberThrows) {
    // std::stoi/std::stod silently accept a numeric prefix and ignore the
    // rest ("2abc" -> 2) unless the caller checks how much was consumed --
    // exactly the bug this test guards against.
    EXPECT_THROW(palim::parseArgs({"--width", "640x480"}), std::invalid_argument);
    EXPECT_THROW(palim::parseArgs({"--threshold", "5percent"}), std::invalid_argument);
}

TEST(ParseArgs, NonNumericThresholdThrows) {
    EXPECT_THROW(palim::parseArgs({"--threshold", "none"}), std::invalid_argument);
}

// A negative or zero width/height parses cleanly as an int but is
// nonsense for a pixel dimension -- and would otherwise get cast to a
// huge V4L2_FMT __u32 in Camera::setFormat rather than fail loudly here.
TEST(ParseArgs, NonPositiveWidthOrHeightThrows) {
    EXPECT_THROW(palim::parseArgs({"--width", "0"}), std::invalid_argument);
    EXPECT_THROW(palim::parseArgs({"--width", "-640"}), std::invalid_argument);
    EXPECT_THROW(palim::parseArgs({"--height", "0"}), std::invalid_argument);
    EXPECT_THROW(palim::parseArgs({"--height", "-480"}), std::invalid_argument);
}

// A negative stable-duration would make "now - stableSince_ >=
// stableDuration_" true on essentially the first tick after any change,
// committing instantly instead of waiting for real stability.
TEST(ParseArgs, NegativeStableMsThrows) {
    EXPECT_THROW(palim::parseArgs({"--stable-ms", "-1"}), std::invalid_argument);
}

TEST(ParseArgs, ZeroStableMsIsAccepted) {
    const auto config = palim::parseArgs({"--stable-ms", "0"});
    EXPECT_EQ(config.stableDuration, std::chrono::milliseconds(0));
}

// std::stod parses "nan"/"inf"/"-inf" as a clean, no-exception success,
// and a plain negative number parses too -- none of them mean anything
// as a change-percentage threshold.
TEST(ParseArgs, NonFiniteOrNegativeThresholdThrows) {
    EXPECT_THROW(palim::parseArgs({"--threshold", "nan"}), std::invalid_argument);
    EXPECT_THROW(palim::parseArgs({"--threshold", "inf"}), std::invalid_argument);
    EXPECT_THROW(palim::parseArgs({"--threshold", "-5"}), std::invalid_argument);
}

TEST(ParseArgs, ZeroThresholdIsAccepted) {
    const auto config = palim::parseArgs({"--threshold", "0"});
    EXPECT_DOUBLE_EQ(config.changeThresholdPercent, 0.0);
}

}  // namespace
