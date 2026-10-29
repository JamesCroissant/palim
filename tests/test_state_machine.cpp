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

    std::optional<palim::CommitEvent> tick(double changedPercent, std::chrono::milliseconds advance,
                                            std::optional<cv::Rect> changedRegion = std::nullopt) {
        now += advance;
        return sm.update(changedPercent, changedRegion, frame, now, settings);
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
    // The commit's changeScore is the 20.0 that originally triggered
    // Stable -> Changing, not 0.0 (the percent on this, the stability-
    // confirming tick itself, which is by definition always at or below
    // the threshold -- reporting that instead would make changeScore
    // meaningless on every single commit).
    EXPECT_DOUBLE_EQ(result->changeScore, 20.0);
}

TEST_F(StateMachineTest, CommitEventHasNoRegionWhenItsTriggeringTickHadNone) {
    tick(0.0, 10ms);
    tick(20.0, 10ms);                      // Stable -> Changing; the triggering tick, no region
    tick(0.0, 10ms);                       // Changing -> WaitForStable (stableSince_ set here)
    auto result = tick(0.0, 210ms);
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->changedRegion.has_value());
}

// The region captured is the *triggering* tick's (Stable -> Changing), not
// whichever tick happens to finally confirm stability -- that tick's own
// region (if it even reported one; stable-by-definition ticks usually
// don't cross the threshold at all) describes incidental noise, not the
// change this commit is actually about.
TEST_F(StateMachineTest, CommitEventReflectsTheRegionFromTheTriggeringTick) {
    const cv::Rect region(10, 20, 30, 40);
    tick(0.0, 10ms);
    tick(20.0, 10ms, region);              // Stable -> Changing; the triggering tick
    tick(0.0, 10ms);                       // Changing -> WaitForStable (stableSince_ set here)
    // The final, stability-confirming tick carries no region of its own --
    // if the bug ever came back (reading the region off *this* tick
    // instead of the triggering one), this would start failing.
    auto result = tick(0.0, 210ms);
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->changedRegion.has_value());
    EXPECT_EQ(*result->changedRegion, region);
}

// Motion resuming mid-WaitForStable sends state_ back to Changing (see
// MotionResumingMidWaitResetsTheClock) without re-triggering from Stable --
// the eventual commit must still carry the *original* trigger's
// changeScore/changedRegion, not the resumption's, the same way
// beforeFrame_ already isn't overwritten by a resumption either.
TEST_F(StateMachineTest, ResumedMotionDoesNotOverwriteTheOriginalTrigger) {
    const cv::Rect firstRegion(1, 2, 3, 4);
    const cv::Rect resumeRegion(50, 60, 70, 80);
    tick(0.0, 10ms);
    tick(20.0, 10ms, firstRegion);         // Stable -> Changing; the real trigger
    tick(0.0, 50ms);                       // Changing -> WaitForStable
    tick(10.0, 10ms, resumeRegion);        // motion resumes -> back to Changing
    tick(0.0, 100ms);                      // Changing -> WaitForStable again
    tick(0.0, 90ms);                       // not enough time yet
    auto result = tick(0.0, 111ms);        // 201ms since the second stableSince_ -> commits
    ASSERT_TRUE(result.has_value());
    EXPECT_DOUBLE_EQ(result->changeScore, 20.0);
    ASSERT_TRUE(result->changedRegion.has_value());
    EXPECT_EQ(*result->changedRegion, firstRegion);
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
