#include <gtest/gtest.h>

#include "timeline_format.hpp"

namespace {

using palim::ChangedRegion;
using palim::CommitMetadata;

TEST(ParseTimelineArgs, DefaultsToCommitsDirWithNoArguments) {
    const auto args = palim::parseTimelineArgs({});
    EXPECT_FALSE(args.helpRequested);
    EXPECT_EQ(args.commitsDir, "commits");
}

TEST(ParseTimelineArgs, PositionalArgumentOverridesTheCommitsDir) {
    const auto args = palim::parseTimelineArgs({"/tmp/my-commits"});
    EXPECT_FALSE(args.helpRequested);
    EXPECT_EQ(args.commitsDir, "/tmp/my-commits");
}

TEST(ParseTimelineArgs, LongHelpFlagSetsHelpRequested) {
    EXPECT_TRUE(palim::parseTimelineArgs({"--help"}).helpRequested);
}

TEST(ParseTimelineArgs, ShortHelpFlagSetsHelpRequested) {
    EXPECT_TRUE(palim::parseTimelineArgs({"-h"}).helpRequested);
}

// Previously the only bug this closes: palim-timeline had no --help
// handling at all, so "--help" was silently treated as the commits
// directory path instead of showing usage.
TEST(ParseTimelineArgs, HelpFlagIsNotMistakenForACommitsDirectory) {
    const auto args = palim::parseTimelineArgs({"--help"});
    EXPECT_TRUE(args.helpRequested);
    EXPECT_EQ(args.commitsDir, "commits");
}

// --help takes priority regardless of position, the same way palim's
// own --help (Phase 22) wins over whatever else is on the command line.
TEST(ParseTimelineArgs, HelpFlagTakesPriorityOverAPositionalArgument) {
    EXPECT_TRUE(palim::parseTimelineArgs({"/some/dir", "--help"}).helpRequested);
    EXPECT_TRUE(palim::parseTimelineArgs({"--help", "/some/dir"}).helpRequested);
}

TEST(FormatShortHash, TruncatesToSevenCharacters) {
    EXPECT_EQ(palim::formatShortHash("a91e32f1234567890"), "a91e32f");
}

TEST(FormatShortHash, LeavesAShorterHashUnchanged) {
    EXPECT_EQ(palim::formatShortHash("abc12"), "abc12");
}

TEST(FormatShortHash, EmptyHashStaysEmpty) {
    EXPECT_EQ(palim::formatShortHash(""), "");
}

TEST(FormatOptionalIntChange, EmptyWhenNothingChanged) {
    EXPECT_EQ(palim::formatOptionalIntChange("gain", 10, 10), "");
    EXPECT_EQ(palim::formatOptionalIntChange("gain", std::nullopt, std::nullopt), "");
}

TEST(FormatOptionalIntChange, FormatsAChangeBetweenTwoValues) {
    EXPECT_EQ(palim::formatOptionalIntChange("exposure", 200, 250), "        exposure: 200 -> 250\n");
}

// std::nullopt means "this device doesn't support the control" -- shown
// as "?" rather than, say, 0 (which would look like a real reading).
TEST(FormatOptionalIntChange, UsesAQuestionMarkForANulloptSide) {
    EXPECT_EQ(palim::formatOptionalIntChange("gain", std::nullopt, 10), "        gain: ? -> 10\n");
    EXPECT_EQ(palim::formatOptionalIntChange("gain", 10, std::nullopt), "        gain: 10 -> ?\n");
}

TEST(FormatGitChange, EmptyWhenEitherSideHasNoGit) {
    CommitMetadata before, after;
    before.gitAvailable = false;
    after.gitAvailable = true;
    after.gitCommit = "abc1234";
    EXPECT_EQ(palim::formatGitChange(before, after), "");
}

TEST(FormatGitChange, EmptyWhenCommitAndDirtyFilesAreIdentical) {
    CommitMetadata before, after;
    before.gitAvailable = after.gitAvailable = true;
    before.gitCommit = after.gitCommit = "abc1234";
    before.dirtyFiles = after.dirtyFiles = {"src/foo.cpp"};
    EXPECT_EQ(palim::formatGitChange(before, after), "");
}

TEST(FormatGitChange, ReportsTheCommitMoving) {
    CommitMetadata before, after;
    before.gitAvailable = after.gitAvailable = true;
    before.gitCommit = "a91e32f1234567890";
    after.gitCommit = "b82f43a9876543210";
    EXPECT_EQ(palim::formatGitChange(before, after), "        git: a91e32f -> b82f43a\n");
}

TEST(FormatGitChange, ReportsNewlyDirtyFiles) {
    CommitMetadata before, after;
    before.gitAvailable = after.gitAvailable = true;
    before.gitCommit = after.gitCommit = "abc1234";
    before.dirtyFiles = {"src/old.cpp"};
    after.dirtyFiles = {"src/old.cpp", "src/new.cpp"};
    EXPECT_EQ(palim::formatGitChange(before, after), "        newly modified: src/new.cpp\n");
}

// The diff is deliberately one-directional: a file that *stopped*
// being dirty (committed in between) isn't reported at all, since this
// answers "what changed for the worse", not a full symmetric diff.
TEST(FormatGitChange, DoesNotReportFilesThatBecameClean) {
    CommitMetadata before, after;
    before.gitAvailable = after.gitAvailable = true;
    before.gitCommit = after.gitCommit = "abc1234";
    before.dirtyFiles = {"src/old.cpp", "src/fixed.cpp"};
    after.dirtyFiles = {"src/old.cpp"};
    EXPECT_EQ(palim::formatGitChange(before, after), "");
}

TEST(FormatResolutionChange, EmptyWhenNeitherDimensionChanged) {
    CommitMetadata before, after;
    before.width = after.width = 1280;
    before.height = after.height = 720;
    EXPECT_EQ(palim::formatResolutionChange(before, after), "");
}

// Reachable in practice: restarting palim against an existing commits/
// directory with different --width/--height resumes numbering (Phase
// 11) rather than starting a fresh experiment, so two commits from
// different invocations of the same experiment can genuinely differ
// here.
TEST(FormatResolutionChange, ReportsAChangeInEitherDimension) {
    CommitMetadata before, after;
    before.width = 1280;
    before.height = 720;
    after.width = 640;
    after.height = 480;
    EXPECT_EQ(palim::formatResolutionChange(before, after), "        resolution: 1280x720 -> 640x480\n");
}

TEST(FormatResolutionChange, ReportsAChangeInOnlyOneDimension) {
    CommitMetadata before, after;
    before.width = after.width = 1280;
    before.height = 720;
    after.height = 1024;
    EXPECT_EQ(palim::formatResolutionChange(before, after), "        resolution: 1280x720 -> 1280x1024\n");
}

TEST(FormatCommit, FormatsTheBasicFieldsWithoutARegionOrGit) {
    CommitMetadata c;
    c.id = "001";
    c.timestamp = "2026-11-03T10:00:00";
    c.changeScore = 12.3;
    c.sharpnessScore = 500.1;
    c.width = 1280;
    c.height = 720;

    const std::string expected =
        "#001  2026-11-03T10:00:00\n"
        "  change: 12.3%   sharpness: 500.1\n"
        "  camera: 1280x720\n";
    EXPECT_EQ(palim::formatCommit(c), expected);
}

TEST(FormatCommit, IncludesTheChangedRegionWhenPresent) {
    CommitMetadata c;
    c.id = "002";
    c.changedRegion = ChangedRegion{10, 20, 30, 40};

    const std::string formatted = palim::formatCommit(c);
    EXPECT_NE(formatted.find("  region: (10,20) 30x40\n"), std::string::npos);
}

TEST(FormatCommit, IncludesOptionalCameraControlsOnlyWhenPresent) {
    CommitMetadata c;
    c.exposureAbsolute = 200;
    c.gain = 10;
    // exposureAuto, whiteBalanceAuto, and whiteBalanceTemperature left as
    // std::nullopt.

    const std::string formatted = palim::formatCommit(c);
    EXPECT_NE(formatted.find("exposure=200"), std::string::npos);
    EXPECT_NE(formatted.find("gain=10"), std::string::npos);
    EXPECT_EQ(formatted.find("exposure_auto="), std::string::npos);
    EXPECT_EQ(formatted.find("white_balance_auto="), std::string::npos);
    EXPECT_EQ(formatted.find("wb="), std::string::npos);
}

// exposureAuto/whiteBalanceAuto have been collected since Phase 5, the
// same as exposureAbsolute/gain/whiteBalanceTemperature just above, but
// formatCommit() never printed either one at all until now -- the same
// "promised in Phase 6's own description (palim-timeline prints 'camera
// settings' per commit), missing in practice" gap Phase 31 already fixed
// once for the between-commit diff block, just at this call site instead.
TEST(FormatCommit, IncludesAutoExposureAndAutoWhiteBalanceWhenPresent) {
    CommitMetadata c;
    c.exposureAuto = 1;
    c.whiteBalanceAuto = 0;

    const std::string formatted = palim::formatCommit(c);
    EXPECT_NE(formatted.find("exposure_auto=1"), std::string::npos);
    EXPECT_NE(formatted.find("white_balance_auto=0"), std::string::npos);
}

TEST(FormatCommit, IncludesGitSummaryWhenAvailable) {
    CommitMetadata c;
    c.gitAvailable = true;
    c.gitCommit = "a91e32f1234567890";
    c.dirtyFiles = {"src/a.cpp", "src/b.cpp"};

    const std::string formatted = palim::formatCommit(c);
    EXPECT_NE(formatted.find("git: a91e32f (2 dirty files)\n"), std::string::npos);
}

TEST(FormatCommit, OmitsGitSummaryWhenUnavailable) {
    CommitMetadata c;
    c.gitAvailable = false;

    EXPECT_EQ(palim::formatCommit(c).find("git:"), std::string::npos);
}

}  // namespace
