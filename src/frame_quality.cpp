#include "frame_quality.hpp"

#include <opencv2/imgproc.hpp>

namespace palim {

double computeSharpness(const cv::Mat& frame) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);

    cv::Scalar mean, stddev;
    cv::meanStdDev(laplacian, mean, stddev);
    return stddev[0] * stddev[0];  // variance = sharpness score
}

}  // namespace palim
