#include "camera.hpp"

namespace palim {

Camera::Camera(int deviceIndex, int width, int height)
    : cap_(deviceIndex, cv::CAP_V4L2) {
    if (cap_.isOpened()) {
        cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
        cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    }
}

bool Camera::isOpened() const {
    return cap_.isOpened();
}

std::optional<cv::Mat> Camera::grab() {
    cv::Mat frame;
    if (!cap_.read(frame) || frame.empty()) {
        return std::nullopt;
    }
    return frame;
}

}  // namespace palim
