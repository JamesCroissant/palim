#include "frame_history.hpp"

#include "frame_quality.hpp"

namespace palim {

FrameHistory::FrameHistory(std::size_t capacity) : buffer_(capacity) {}

void FrameHistory::push(cv::Mat frame, std::chrono::steady_clock::time_point timestamp) {
    // clone(): VideoCapture backends can reuse an internal buffer across
    // reads, so holding onto a frame past the next grab() call needs an
    // independent copy, not just another reference to the same memory.
    buffer_.push(FrameSample{timestamp, frame.clone()});
}

std::optional<cv::Mat> FrameHistory::bestFrameInRange(std::chrono::steady_clock::time_point from,
                                                        std::chrono::steady_clock::time_point to) const {
    std::optional<cv::Mat> best;
    double bestScore = -1.0;

    for (const auto& sample : buffer_.snapshot()) {
        if (sample.timestamp < from || sample.timestamp > to) {
            continue;
        }
        const double score = computeSharpness(sample.frame);
        if (score > bestScore) {
            bestScore = score;
            best = sample.frame;
        }
    }
    return best;
}

}  // namespace palim
