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
                                           "--threshold", "8", "--stable-ms", "1500", "--output-dir", "out"});
    EXPECT_EQ(config.devicePath, "/dev/video3");
    EXPECT_EQ(config.width, 320);
    EXPECT_EQ(config.height, 240);
    EXPECT_DOUBLE_EQ(config.changeThresholdPercent, 8.0);
    EXPECT_EQ(config.stableDuration, std::chrono::milliseconds(1500));
    EXPECT_EQ(config.outputDir, "out");
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

}  // namespace
