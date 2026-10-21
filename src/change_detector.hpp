#pragma once

#include <optional>

#include <opencv2/core.hpp>

namespace palim {

// Result of compareWithRegion(): the same changed-% compare() reports,
// plus where on the frame that change was concentrated.
struct ChangeResult {
    double changedPercent = 0.0;
    // The smallest axis-aligned rectangle containing every pixel counted
    // as changed. nullopt iff changedPercent's mask was entirely empty
    // (no pixel crossed pixelDiffThreshold_) -- there's no meaningful
    // bounding box of nothing.
    std::optional<cv::Rect> changedRegion;
};

// Stateless: given two frames, returns what fraction of pixels changed
// (and, via compareWithRegion, where). Doesn't decide whether that amount
// of change "means" anything -- that judgment (and the state it
// requires) belongs to StateMachine.
class ChangeDetector {
public:
    // pixelDiffThreshold: minimum per-pixel intensity difference (0-255)
    // to count as "changed", after blurring. Filters out sensor noise
    // and small lighting flicker.
    explicit ChangeDetector(int pixelDiffThreshold = 25, int blurKernelSize = 21);

    // Returns percentage (0.0-100.0) of pixels that differ between the
    // two frames. Frames must be the same size.
    double compare(const cv::Mat& prevFrame, const cv::Mat& currFrame) const;

    // Same comparison as compare(), but also reports the bounding box of
    // where the frame changed -- a first, concrete step toward the
    // spec's "what changed physically?" goal (section 4): not *what*
    // changed, just *where*, but that's still strictly more than a bare
    // percentage says.
    ChangeResult compareWithRegion(const cv::Mat& prevFrame, const cv::Mat& currFrame) const;

private:
    cv::Mat changedMask(const cv::Mat& prevFrame, const cv::Mat& currFrame) const;
    static cv::Mat preprocess(const cv::Mat& frame, int blurKernelSize);

    int pixelDiffThreshold_;
    int blurKernelSize_;
};

}  // namespace palim
