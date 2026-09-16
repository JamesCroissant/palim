#include <gtest/gtest.h>

#include "camera.hpp"

namespace {

// This environment has no /dev/video0, so these tests exercise the
// failure paths: opening a device that doesn't exist, and opening a
// file that exists but isn't a V4L2 device. Actual capture (grab())
// against a real camera is untestable here and is covered by manual
// verification against real hardware instead.

TEST(Camera, MissingDeviceFailsToOpen) {
    palim::Camera camera("/dev/does-not-exist-for-palim-tests");
    EXPECT_FALSE(camera.isOpened());
}

TEST(Camera, NonV4L2DeviceFailsAtQueryCap) {
    // /dev/null opens fine but isn't a V4L2 device, so VIDIOC_QUERYCAP
    // must be what rejects it.
    palim::Camera camera("/dev/null");
    EXPECT_FALSE(camera.isOpened());
}

TEST(Camera, CurrentSettingsOnUnopenedCameraReturnsAllNullopt) {
    palim::Camera camera("/dev/does-not-exist-for-palim-tests");
    auto settings = camera.currentSettings();
    EXPECT_FALSE(settings.exposureAuto.has_value());
    EXPECT_FALSE(settings.exposureAbsolute.has_value());
    EXPECT_FALSE(settings.gain.has_value());
    EXPECT_FALSE(settings.whiteBalanceAuto.has_value());
    EXPECT_FALSE(settings.whiteBalanceTemperature.has_value());
}

TEST(Camera, RepeatedConstructionAndMoveDoesNotCrash) {
    for (int i = 0; i < 20; ++i) {
        palim::Camera camera("/dev/null");
        (void)camera.isOpened();
    }

    palim::Camera a("/dev/null");
    palim::Camera b(std::move(a));
    palim::Camera c("/dev/null");
    c = std::move(b);
    SUCCEED();
}

}  // namespace
