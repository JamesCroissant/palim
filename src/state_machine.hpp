#pragma once

#include <chrono>
#include <optional>

#include <opencv2/core.hpp>

#include "camera_settings.hpp"

namespace palim {

// Result of a completed commit: the frame from just before the change
// started, and the frame once things settled back down.
//
// `after` is the single frame captured at the exact instant stability was
// confirmed — a reasonable default. stableWindowStart/End mark the span
// during which the desk held still, so a caller with access to recent
// frame history (FrameHistory) can pick a sharper candidate from within
// that window instead, and overwrite `after` before saving.
//
// `settings` is the camera's control state at the same instant as
// `after` -- not re-queried per candidate frame during best-frame
// selection, since exposure/gain/white-balance change far more slowly
// than frame-to-frame.
struct CommitEvent {
    cv::Mat before;
    cv::Mat after;
    double changeScore;  // the changed-% reading that triggered CHANGING
    // Bounding box of where that same changed-% reading was concentrated,
    // in the coordinate space of the frame that produced it. nullopt
    // under the same condition ChangeDetector::compareWithRegion()
    // reports it: no pixel crossed the detector's threshold.
    std::optional<cv::Rect> changedRegion;
    std::chrono::steady_clock::time_point stableWindowStart;
    std::chrono::steady_clock::time_point stableWindowEnd;
    CameraSettings settings;
};

// Implements the STABLE -> CHANGING -> WAIT_FOR_STABLE -> commit cycle
// from the design doc. This is the only class in the pipeline that holds
// state across frames; everything upstream (Camera, ChangeDetector) and
// downstream (SnapshotWriter) is stateless.
class StateMachine {
public:
    StateMachine(double changeThresholdPercent, std::chrono::milliseconds stableDuration);

    // Call once per frame with the change-% (and, if any, the changed
    // region) relative to the previous frame. Returns a CommitEvent only
    // on the frame that completes a stable period after a change.
    std::optional<CommitEvent> update(double changedPercent, std::optional<cv::Rect> changedRegion,
                                       const cv::Mat& frame, std::chrono::steady_clock::time_point now,
                                       const CameraSettings& settings);

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
