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

TEST(ChangeDetectorRegion, IdenticalFramesHaveNoRegion) {
    cv::Mat a(480, 640, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::Mat b = a.clone();

    palim::ChangeDetector detector;
    const auto result = detector.compareWithRegion(a, b);
    EXPECT_DOUBLE_EQ(result.changedPercent, 0.0);
    EXPECT_FALSE(result.changedRegion.has_value());
}

TEST(ChangeDetectorRegion, ReportsTheSamePercentAsCompare) {
    cv::Mat a(480, 640, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::Mat b = a.clone();
    b(cv::Rect(0, 0, 640, 240)).setTo(cv::Scalar(250, 250, 250));

    palim::ChangeDetector detector;
    EXPECT_DOUBLE_EQ(detector.compareWithRegion(a, b).changedPercent, detector.compare(a, b));
}

// A change confined to one corner should report a region close to that
// corner's bounds, not the whole frame -- the entire point of this
// feature over the bare percentage compare() already returns.
TEST(ChangeDetectorRegion, LocalizedChangeReportsATightBoundingBox) {
    cv::Mat a(480, 640, CV_8UC3, cv::Scalar(100, 100, 100));
    cv::Mat b = a.clone();
    const cv::Rect changedArea(400, 300, 150, 100);  // bottom-right-ish, well clear of edges
    b(changedArea).setTo(cv::Scalar(250, 250, 250));

    palim::ChangeDetector detector;
    const auto result = detector.compareWithRegion(a, b);
    ASSERT_TRUE(result.changedRegion.has_value());

    const cv::Rect& region = *result.changedRegion;
    // GaussianBlur bleeds the edges by roughly half the blur kernel
    // (default 21, so ~10px) in each direction -- allow generous slack
    // rather than pin an exact pixel match to an implementation detail.
    constexpr int kSlack = 15;
    EXPECT_GE(region.x, changedArea.x - kSlack);
    EXPECT_LE(region.x, changedArea.x + kSlack);
    EXPECT_GE(region.y, changedArea.y - kSlack);
    EXPECT_LE(region.y, changedArea.y + kSlack);
    EXPECT_LE(region.br().x, changedArea.br().x + kSlack);
    EXPECT_LE(region.br().y, changedArea.br().y + kSlack);
    // The real point: tight, not "the whole 640x480 frame".
    EXPECT_LT(region.width, 640);
    EXPECT_LT(region.height, 480);
}

}  // namespace
