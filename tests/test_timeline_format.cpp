#include <gtest/gtest.h>

#include "timeline_format.hpp"

namespace {

using palim::ChangedRegion;
using palim::CommitMetadata;

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
    // whiteBalanceTemperature left as std::nullopt.

    const std::string formatted = palim::formatCommit(c);
    EXPECT_NE(formatted.find("exposure=200"), std::string::npos);
    EXPECT_NE(formatted.find("gain=10"), std::string::npos);
    EXPECT_EQ(formatted.find("wb="), std::string::npos);
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
