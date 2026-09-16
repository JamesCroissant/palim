#pragma once

#include <chrono>
#include <optional>

#include <opencv2/core.hpp>

#include "camera_settings.hpp"
#include "ring_buffer.hpp"

namespace palim {

struct FrameSample {
    std::chrono::steady_clock::time_point timestamp{};
    cv::Mat frame;
    CameraSettings settings;
};

// Keeps the last few seconds of frames so a commit can pick the sharpest
// one from a time window, instead of whatever frame happened to be
// current the instant the state machine fired.
class FrameHistory {
public:
    explicit FrameHistory(std::size_t capacity);

    void push(cv::Mat frame, std::chrono::steady_clock::time_point timestamp);

    // Sharpest frame with timestamp in [from, to], or nullopt if none of
    // the buffered frames fall in that range.
    std::optional<cv::Mat> bestFrameInRange(std::chrono::steady_clock::time_point from,
                                             std::chrono::steady_clock::time_point to) const;

private:
    RingBuffer<FrameSample> buffer_;
};

}  // namespace palim
