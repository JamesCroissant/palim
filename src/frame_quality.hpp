#pragma once

#include <opencv2/core.hpp>

namespace palim {

// Sharpness score via Laplacian variance: higher means more in-focus /
// less motion-blurred. Shared by SnapshotWriter (records the number) and
// FrameHistory (uses it to pick the best of several candidates).
double computeSharpness(const cv::Mat& frame);

}  // namespace palim
