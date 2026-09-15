#include "state_machine.hpp"

namespace palim {

StateMachine::StateMachine(double changeThresholdPercent, std::chrono::milliseconds stableDuration)
    : changeThresholdPercent_(changeThresholdPercent), stableDuration_(stableDuration) {}

std::optional<CommitEvent> StateMachine::update(double changedPercent, const cv::Mat& frame,
                                                  std::chrono::steady_clock::time_point now) {
    std::optional<CommitEvent> event;
    const bool changing = changedPercent > changeThresholdPercent_;

    switch (state_) {
        case State::Stable:
            if (changing) {
                // lastFrame_ is the most recent frame we saw while still
                // stable — i.e. the state of the desk just before this
                // change started.
                beforeFrame_ = lastFrame_;
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
                event = CommitEvent{beforeFrame_, frame, changedPercent, stableSince_, now};
                state_ = State::Stable;
            }
            break;
    }

    lastFrame_ = frame;
    return event;
}

}  // namespace palim
