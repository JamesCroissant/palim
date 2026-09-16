#include <gtest/gtest.h>

#include "change_detector.hpp"

namespace {

TEST(ChangeDetector, IdenticalFramesReadZero) {
    cv::Mat a(480, 640, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::Mat b = a.clone();

    palim::ChangeDetector detector;
    EXPECT_DOUBLE_EQ(detector.compare(a, b), 0.0);
}

TEST(ChangeDetector, LargeChangeReadsRoughlyProportional) {
    cv::Mat a(480, 640, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::Mat b = a.clone();
    b(cv::Rect(0, 0, 640, 240)).setTo(cv::Scalar(250, 250, 250));  // top half changed drastically

    palim::ChangeDetector detector;
    const double changed = detector.compare(a, b);

    // Should read close to 50% (blur bleed at the boundary keeps it from
    // being exact).
    EXPECT_GT(changed, 45.0);
    EXPECT_LT(changed, 55.0);
}

TEST(ChangeDetector, SubThresholdNoiseReadsZero) {
    cv::Mat a(480, 640, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::Mat b(480, 640, CV_8UC3, cv::Scalar(105, 105, 105));  // +5, below default threshold of 25

    palim::ChangeDetector detector;
    EXPECT_DOUBLE_EQ(detector.compare(a, b), 0.0);
}

}  // namespace
