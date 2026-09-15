#pragma once

#include <chrono>
#include <optional>

#include <opencv2/core.hpp>

namespace palim {

// Result of a completed commit: the frame from just before the change
// started, and the frame once things settled back down.
struct CommitEvent {
    cv::Mat before;
    cv::Mat after;
    double changeScore;  // the changed-% reading that triggered CHANGING
};

// Implements the STABLE -> CHANGING -> WAIT_FOR_STABLE -> commit cycle
// from the design doc. This is the only class in the pipeline that holds
// state across frames; everything upstream (Camera, ChangeDetector) and
// downstream (SnapshotWriter) is stateless.
class StateMachine {
public:
    StateMachine(double changeThresholdPercent, std::chrono::milliseconds stableDuration);

    // Call once per frame with the change-% relative to the previous
    // frame. Returns a CommitEvent only on the frame that completes a
    // stable period after a change.
    std::optional<CommitEvent> update(double changedPercent, const cv::Mat& frame,
                                       std::chrono::steady_clock::time_point now);

private:
    enum class State { Stable, Changing, WaitForStable };

    double changeThresholdPercent_;
    std::chrono::milliseconds stableDuration_;

    State state_ = State::Stable;
    cv::Mat lastFrame_;
    cv::Mat beforeFrame_;
    std::chrono::steady_clock::time_point stableSince_;
};

}  // namespace palim
