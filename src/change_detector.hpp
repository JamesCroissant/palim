#pragma once

#include <opencv2/core.hpp>

namespace palim {

// Stateless: given two frames, returns what fraction of pixels changed.
// Doesn't decide whether that amount of change "means" anything —
// that judgment (and the state it requires) belongs to StateMachine.
class ChangeDetector {
public:
    // pixelDiffThreshold: minimum per-pixel intensity difference (0-255)
    // to count as "changed", after blurring. Filters out sensor noise
    // and small lighting flicker.
    explicit ChangeDetector(int pixelDiffThreshold = 25, int blurKernelSize = 21);

    // Returns percentage (0.0-100.0) of pixels that differ between the
    // two frames. Frames must be the same size.
    double compare(const cv::Mat& prevFrame, const cv::Mat& currFrame) const;

private:
    static cv::Mat preprocess(const cv::Mat& frame, int blurKernelSize);

    int pixelDiffThreshold_;
    int blurKernelSize_;
};

}  // namespace palim
