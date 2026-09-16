#include <gtest/gtest.h>

#include "state_machine.hpp"

using namespace std::chrono_literals;

namespace {

class StateMachineTest : public ::testing::Test {
protected:
    palim::StateMachine sm{5.0, 200ms};  // short duration so tests run fast
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    cv::Mat frame{10, 10, CV_8UC3, cv::Scalar(0, 0, 0)};
    palim::CameraSettings settings;

    std::optional<palim::CommitEvent> tick(double changedPercent, std::chrono::milliseconds advance) {
        now += advance;
        return sm.update(changedPercent, frame, now, settings);
    }
};

TEST_F(StateMachineTest, StaysStableBelowThreshold) {
    EXPECT_FALSE(tick(0.0, 10ms));
    EXPECT_FALSE(tick(1.0, 10ms));
}

TEST_F(StateMachineTest, DoesNotCommitWhileChanging) {
    tick(0.0, 10ms);
    EXPECT_FALSE(tick(20.0, 10ms));
    EXPECT_FALSE(tick(15.0, 10ms));
}

TEST_F(StateMachineTest, DoesNotCommitBeforeStableDurationElapses) {
    tick(0.0, 10ms);
    tick(20.0, 10ms);
    EXPECT_FALSE(tick(0.0, 50ms));  // motion stopped, but only 50ms of 200ms required
}

TEST_F(StateMachineTest, MotionResumingMidWaitResetsTheClock) {
    tick(0.0, 10ms);
    tick(20.0, 10ms);
    tick(0.0, 50ms);       // WAIT_FOR_STABLE begins
    EXPECT_FALSE(tick(10.0, 10ms));  // motion resumes -> back to CHANGING
    EXPECT_FALSE(tick(0.0, 100ms));  // only 100ms since the resume-stop, not enough yet
}

TEST_F(StateMachineTest, CommitsOnceStableDurationElapsesAfterChange) {
    tick(0.0, 10ms);
    tick(20.0, 10ms);
    tick(0.0, 50ms);    // WaitForStable begins, stableSince_ set on this tick
    tick(10.0, 10ms);   // motion resumes -> back to Changing
    tick(0.0, 100ms);   // Changing -> WaitForStable again; stableSince_ reset on this tick

    // Elapsed since the second stableSince_: 90ms so far -- not enough.
    EXPECT_FALSE(tick(0.0, 90ms));

    // Elapsed since the second stableSince_: 90 + 111 = 201ms -- now it commits.
    auto result = tick(0.0, 111ms);
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->changeScore, 0.0);
}

TEST_F(StateMachineTest, ReturnsToStableAfterCommitAndDoesNotDoubleCommit) {
    tick(0.0, 10ms);                       // Stable
    tick(20.0, 10ms);                      // Stable -> Changing
    tick(0.0, 10ms);                       // Changing -> WaitForStable (stableSince_ set here)
    ASSERT_TRUE(tick(0.0, 210ms));         // 210ms elapsed -> commits, back to Stable

    // No spurious second commit once back in Stable.
    EXPECT_FALSE(tick(0.0, 10ms));
}

}  // namespace
