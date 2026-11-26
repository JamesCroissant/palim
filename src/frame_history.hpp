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
    // Only what FrameHistory itself needs to pick the sharpest frame in
    // a time range -- unlike FrameSample above (the capture-to-
    // processing queue message, which also carries the camera's
    // settings at capture time for the eventual commit), nothing here
    // ever reads a buffered frame's settings back out, so storing one
    // in every entry would just be a CameraSettings copied for no
    // purpose on every push(). Enabling -Wextra (it wasn't on at all
    // before) caught this directly: reusing FrameSample here left its
    // settings field permanently default-constructed and unread, which
    // -Wmissing-field-initializers flagged as a real aggregate-init
    // call, not a style nit.
    struct BufferedFrame {
        std::chrono::steady_clock::time_point timestamp{};
        cv::Mat frame;
    };

    RingBuffer<BufferedFrame> buffer_;
};

}  // namespace palim
