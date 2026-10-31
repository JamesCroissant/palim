#include "state_machine.hpp"

namespace palim {

StateMachine::StateMachine(double changeThresholdPercent, std::chrono::milliseconds stableDuration)
    : changeThresholdPercent_(changeThresholdPercent), stableDuration_(stableDuration) {}

std::optional<CommitEvent> StateMachine::update(double changedPercent, std::optional<cv::Rect> changedRegion,
                                                 const cv::Mat& frame, std::chrono::steady_clock::time_point now,
                                                 const CameraSettings& settings) {
    std::optional<CommitEvent> event;
    const bool changing = changedPercent > changeThresholdPercent_;
    // lastFrame_ is still its default-constructed, empty self until the
    // very end of this function's first-ever call sets it to a real
    // frame -- so on that first call, there is no genuine "frame just
    // before this one" to fall back on yet. Letting a Stable -> Changing
    // transition through anyway would capture that empty Mat as
    // beforeFrame_, and SnapshotWriter::write() crashes trying to
    // cv::imwrite() an empty image. Camera auto-exposure/white-balance
    // settling in the first couple of real frames, or the desk already
    // mid-motion when palim starts, both make this concretely reachable,
    // not just a theoretical gap.
    const bool haveBaseline = !lastFrame_.empty();

    switch (state_) {
        case State::Stable:
            if (haveBaseline && changing) {
                // lastFrame_ is the most recent frame we saw while still
                // stable — i.e. the state of the desk just before this
                // change started. changedPercent/changedRegion at this
                // exact instant describe the change that triggered this
                // commit cycle -- captured now, not read off whichever
                // tick eventually confirms stability (see the eventual
                // CommitEvent below, and these two fields' own comments
                // in state_machine.hpp).
                beforeFrame_ = lastFrame_;
                triggerChangeScore_ = changedPercent;
                triggerChangedRegion_ = changedRegion;
                state_ = State::Changing;
            }
            break;

        case State::Changing:
            if (!changing) {
                state_ = State::WaitForStable;
                stableSince_ = now;
            }
            break;

        case State::WaitForStable:
            if (changing) {
                // Motion resumed before we reached stableDuration_; treat
                // it as still one continuous change.
                state_ = State::Changing;
            } else if (now - stableSince_ >= stableDuration_) {
                event = CommitEvent{beforeFrame_,          frame,       triggerChangeScore_,
                                     triggerChangedRegion_, stableSince_, now,
                                     settings};
                state_ = State::Stable;
            }
            break;
    }

    lastFrame_ = frame;
    return event;
}

}  // namespace palim
