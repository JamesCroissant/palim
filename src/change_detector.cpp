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

double ChangeDetector::compare(const cv::Mat& prevFrame, const cv::Mat& currFrame) const {
    cv::Mat prevProcessed = preprocess(prevFrame, blurKernelSize_);
    cv::Mat currProcessed = preprocess(currFrame, blurKernelSize_);

    cv::Mat diff;
    cv::absdiff(prevProcessed, currProcessed, diff);

    cv::Mat changedMask;
    cv::threshold(diff, changedMask, pixelDiffThreshold_, 255, cv::THRESH_BINARY);

    const int changedPixels = cv::countNonZero(changedMask);
    const int totalPixels = changedMask.rows * changedMask.cols;

    return (static_cast<double>(changedPixels) / totalPixels) * 100.0;
}

}  // namespace palim
