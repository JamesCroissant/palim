#include <gtest/gtest.h>

#include "display_availability.hpp"

namespace {

TEST(DisplayAvailable, NeitherSetIsUnavailable) {
    EXPECT_FALSE(palim::displayAvailable(nullptr, nullptr));
}

// std::getenv() can't return an empty string for a variable that's
// genuinely unset (it returns nullptr instead), but a variable
// explicitly set to an empty value (`DISPLAY=`) is a real, if unusual,
// shell state -- and just as useless to cv::namedWindow() as not being
// set at all.
TEST(DisplayAvailable, EmptyStringsCountAsUnset) {
    EXPECT_FALSE(palim::displayAvailable("", ""));
    EXPECT_FALSE(palim::displayAvailable("", nullptr));
    EXPECT_FALSE(palim::displayAvailable(nullptr, ""));
}

TEST(DisplayAvailable, X11DisplayAloneIsAvailable) {
    EXPECT_TRUE(palim::displayAvailable(":0", nullptr));
}

TEST(DisplayAvailable, WaylandDisplayAloneIsAvailable) {
    EXPECT_TRUE(palim::displayAvailable(nullptr, "wayland-0"));
}

TEST(DisplayAvailable, BothSetIsAvailable) {
    EXPECT_TRUE(palim::displayAvailable(":0", "wayland-0"));
}

}  // namespace
