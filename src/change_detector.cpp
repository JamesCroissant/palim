#include "change_detector.hpp"

#include <opencv2/imgproc.hpp>

namespace palim {

ChangeDetector::ChangeDetector(int pixelDiffThreshold, int blurKernelSize)
    : pixelDiffThreshold_(pixelDiffThreshold), blurKernelSize_(blurKernelSize) {}

cv::Mat ChangeDetector::preprocess(const cv::Mat& frame, int blurKernelSize) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(blurKernelSize, blurKernelSize), 0);
    return blurred;
}

cv::Mat ChangeDetector::changedMask(const cv::Mat& prevFrame, const cv::Mat& currFrame) const {
    cv::Mat prevProcessed = preprocess(prevFrame, blurKernelSize_);
    cv::Mat currProcessed = preprocess(currFrame, blurKernelSize_);

    cv::Mat diff;
    cv::absdiff(prevProcessed, currProcessed, diff);

    cv::Mat mask;
    cv::threshold(diff, mask, pixelDiffThreshold_, 255, cv::THRESH_BINARY);
    return mask;
}

double ChangeDetector::compare(const cv::Mat& prevFrame, const cv::Mat& currFrame) const {
    const cv::Mat mask = changedMask(prevFrame, currFrame);

    const int changedPixels = cv::countNonZero(mask);
    const int totalPixels = mask.rows * mask.cols;

    return (static_cast<double>(changedPixels) / totalPixels) * 100.0;
}

ChangeResult ChangeDetector::compareWithRegion(const cv::Mat& prevFrame, const cv::Mat& currFrame) const {
    const cv::Mat mask = changedMask(prevFrame, currFrame);

    const int changedPixels = cv::countNonZero(mask);
    const int totalPixels = mask.rows * mask.cols;

    ChangeResult result;
    result.changedPercent = (static_cast<double>(changedPixels) / totalPixels) * 100.0;
    // boundingRect on an all-zero mask returns Rect(0,0,0,0) -- a
    // zero-area rectangle isn't a meaningful "where", so only report one
    // when something actually crossed the threshold.
    if (changedPixels > 0) {
        result.changedRegion = cv::boundingRect(mask);
    }
    return result;
}

}  // namespace palim
