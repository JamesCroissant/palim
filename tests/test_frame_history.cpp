#include <gtest/gtest.h>

#include "frame_history.hpp"
#include "frame_quality.hpp"

using namespace std::chrono_literals;

namespace {

cv::Mat solidFrame(int shade) {
    return cv::Mat(100, 100, CV_8UC3, cv::Scalar(shade, shade, shade));
}

class FrameHistoryTest : public ::testing::Test {
protected:
    palim::FrameHistory history{10};
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    std::vector<cv::Mat> candidates;

    void SetUp() override {
        // Five frames with increasing noise (-> increasing sharpness),
        // at t0, t0+10ms, ..., t0+40ms.
        for (int i = 0; i < 5; ++i) {
            cv::Mat frame = solidFrame(128);
            cv::Mat noise(frame.size(), frame.type());
            cv::randu(noise, cv::Scalar::all(0), cv::Scalar::all(10 * i));
            frame += noise;
            candidates.push_back(frame.clone());
            history.push(frame, t0 + std::chrono::milliseconds(10 * i));
        }
    }
};

TEST_F(FrameHistoryTest, FullRangePicksTheSharpestCandidate) {
    auto best = history.bestFrameInRange(t0, t0 + 40ms);
    ASSERT_TRUE(best.has_value());
    EXPECT_DOUBLE_EQ(palim::computeSharpness(*best), palim::computeSharpness(candidates[4]));
}

TEST_F(FrameHistoryTest, NarrowedRangeExcludesLaterSharperCandidates) {
    // Only indices 0..2 (t0..t0+20ms) are eligible; index 2 should win
    // among those even though index 4 is sharper overall.
    auto best = history.bestFrameInRange(t0, t0 + 20ms);
    ASSERT_TRUE(best.has_value());
    EXPECT_DOUBLE_EQ(palim::computeSharpness(*best), palim::computeSharpness(candidates[2]));
}

TEST_F(FrameHistoryTest, RangeBeforeAnyPushedFrameReturnsNullopt) {
    auto result = history.bestFrameInRange(t0 - 100ms, t0 - 50ms);
    EXPECT_FALSE(result.has_value());
}

}  // namespace
