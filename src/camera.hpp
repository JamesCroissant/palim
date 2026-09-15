#pragma once

#include <optional>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

namespace palim {

// Owns a single V4L2 capture device. One Camera == one open device handle;
// copying would mean two objects racing to release the same fd, so it's
// non-copyable. Move is allowed since ownership can transfer cleanly.
class Camera {
public:
    explicit Camera(int deviceIndex, int width = 1280, int height = 720);

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    Camera(Camera&&) = default;
    Camera& operator=(Camera&&) = default;

    bool isOpened() const;

    // Returns the next frame, or std::nullopt if the read failed
    // (device hiccup, disconnect, etc.) instead of throwing.
    std::optional<cv::Mat> grab();

private:
    cv::VideoCapture cap_;
};

}  // namespace palim
