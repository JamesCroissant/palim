#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

#include "frame_quality.hpp"

namespace {

cv::Mat solidFrame(int shade) {
    return cv::Mat(100, 100, CV_8UC3, cv::Scalar(shade, shade, shade));
}

// A perfectly uniform frame has a constant Laplacian (zero everywhere), so
// its variance -- computeSharpness()'s whole definition -- is exactly 0.
// FrameHistoryTest's own frames are never perfectly uniform (noise is
// added to every candidate, including index 0), so this exact boundary
// was never actually exercised anywhere.
TEST(ComputeSharpnessTest, SolidFrameHasZeroSharpness) {
    EXPECT_DOUBLE_EQ(palim::computeSharpness(solidFrame(128)), 0.0);
}

// Same reasoning: the two darkest/lightest ends of the shade range should
// be exactly as flat as the frame already tested above, not just "close
// to 0" for some arbitrary shade.
TEST(ComputeSharpnessTest, SolidFrameIsZeroSharpnessRegardlessOfShade) {
    EXPECT_DOUBLE_EQ(palim::computeSharpness(solidFrame(0)), 0.0);
    EXPECT_DOUBLE_EQ(palim::computeSharpness(solidFrame(255)), 0.0);
}

// A frame with real edge content (half black, half white) has a nonzero
// Laplacian response at the boundary, so its score must be strictly
// greater than a uniform frame's -- the documented "higher means more
// in-focus" contract, pinned down with concrete content rather than only
// ever comparing two already-noisy frames to each other.
TEST(ComputeSharpnessTest, FrameWithASharpEdgeHasPositiveSharpness) {
    cv::Mat frame = solidFrame(0);
    frame(cv::Rect(50, 0, 50, 100)).setTo(cv::Scalar(255, 255, 255));
    EXPECT_GT(palim::computeSharpness(frame), 0.0);
}

// Blurring a frame softens its edges, which is exactly what this score is
// meant to detect (it's used to pick the sharpest, least motion-blurred
// candidate frame) -- so a blurred copy of the same edge frame must score
// lower than the original, not just "some different number."
TEST(ComputeSharpnessTest, BlurringAFrameLowersItsSharpnessScore) {
    cv::Mat sharp = solidFrame(0);
    sharp(cv::Rect(50, 0, 50, 100)).setTo(cv::Scalar(255, 255, 255));

    cv::Mat blurred;
    cv::GaussianBlur(sharp, blurred, cv::Size(15, 15), 0);

    EXPECT_GT(palim::computeSharpness(sharp), palim::computeSharpness(blurred));
}

}  // namespace
