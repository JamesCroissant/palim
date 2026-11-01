#include <gtest/gtest.h>

#include <fstream>

#include "snapshot_writer.hpp"

namespace {

cv::Mat solidFrame(int shade) { return cv::Mat(40, 40, CV_8UC3, cv::Scalar(shade, shade, shade)); }

palim::CommitEvent makeCommitEvent() {
    palim::CommitEvent event;
    event.before = solidFrame(50);
    event.after = solidFrame(200);
    event.changeScore = 42.0;
    event.stableWindowStart = std::chrono::steady_clock::now();
    event.stableWindowEnd = event.stableWindowStart + std::chrono::seconds(1);
    return event;
}

class SnapshotWriterTest : public ::testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "palim_test_snapshot_writer";

    void SetUp() override { std::filesystem::remove_all(dir); }
    void TearDown() override { std::filesystem::remove_all(dir); }
};

TEST_F(SnapshotWriterTest, FirstCommitInAFreshDirectoryIsNumbered001) {
    palim::SnapshotWriter writer(dir);
    writer.write(makeCommitEvent());

    EXPECT_TRUE(std::filesystem::exists(dir / "001" / "before.jpg"));
    EXPECT_TRUE(std::filesystem::exists(dir / "001" / "after.jpg"));
    EXPECT_TRUE(std::filesystem::exists(dir / "001" / "metadata.json"));
}

TEST_F(SnapshotWriterTest, SuccessiveCommitsInOneRunIncrementSequentially) {
    palim::SnapshotWriter writer(dir);
    writer.write(makeCommitEvent());
    writer.write(makeCommitEvent());
    writer.write(makeCommitEvent());

    EXPECT_TRUE(std::filesystem::exists(dir / "001"));
    EXPECT_TRUE(std::filesystem::exists(dir / "002"));
    EXPECT_TRUE(std::filesystem::exists(dir / "003"));
}

// The bug this test guards against: a second SnapshotWriter constructed
// against a directory that already has commits 001-003 (e.g. after
// restarting palim) must continue at 004, not restart at 001 and
// silently overwrite the real commits already there.
TEST_F(SnapshotWriterTest, ResumingInAnExistingDirectoryDoesNotOverwritePriorCommits) {
    {
        palim::SnapshotWriter firstRun(dir);
        firstRun.write(makeCommitEvent());
        firstRun.write(makeCommitEvent());
        firstRun.write(makeCommitEvent());
    }

    // Mark commit 002 with a sentinel file, so overwriting that
    // directory (not just skipping the ID) would be caught too.
    std::ofstream sentinel(dir / "002" / "sentinel.txt");
    sentinel << "do not touch";
    sentinel.close();

    {
        palim::SnapshotWriter secondRun(dir);
        secondRun.write(makeCommitEvent());
    }

    EXPECT_TRUE(std::filesystem::exists(dir / "004"));
    EXPECT_FALSE(std::filesystem::exists(dir / "005"));
    ASSERT_TRUE(std::filesystem::exists(dir / "002" / "sentinel.txt"));
    std::ifstream check(dir / "002" / "sentinel.txt");
    std::string contents;
    std::getline(check, contents);
    EXPECT_EQ(contents, "do not touch");
}

// A non-numeric subdirectory (something a human created, or a future
// unrelated output) must not confuse the scan into throwing or into
// picking a bogus "highest ID".
TEST_F(SnapshotWriterTest, IgnoresNonNumericSubdirectories) {
    std::filesystem::create_directories(dir / "not-a-commit");
    std::filesystem::create_directories(dir / "001");

    palim::SnapshotWriter writer(dir);
    writer.write(makeCommitEvent());

    EXPECT_TRUE(std::filesystem::exists(dir / "002"));
}

// maxCommits defaults to 0 (unlimited) -- writing well past any
// plausible cap must never delete anything, matching this project's
// behavior before the cap existed.
TEST_F(SnapshotWriterTest, DefaultMaxCommitsOfZeroNeverPrunes) {
    palim::SnapshotWriter writer(dir);
    for (int i = 0; i < 5; ++i) {
        writer.write(makeCommitEvent());
    }

    EXPECT_TRUE(std::filesystem::exists(dir / "001"));
    EXPECT_TRUE(std::filesystem::exists(dir / "005"));
}

// With a cap set, writing past it deletes the oldest (lowest-numbered)
// commits first -- the most recent maxCommits ones survive.
TEST_F(SnapshotWriterTest, WritingPastMaxCommitsDeletesTheOldestFirst) {
    palim::SnapshotWriter writer(dir, /*maxCommits=*/3);
    for (int i = 0; i < 5; ++i) {
        writer.write(makeCommitEvent());
    }

    EXPECT_FALSE(std::filesystem::exists(dir / "001"));
    EXPECT_FALSE(std::filesystem::exists(dir / "002"));
    EXPECT_TRUE(std::filesystem::exists(dir / "003"));
    EXPECT_TRUE(std::filesystem::exists(dir / "004"));
    EXPECT_TRUE(std::filesystem::exists(dir / "005"));
}

// A backlog from a previous run under a larger (or no) cap is trimmed
// down to a newly-lowered cap in one pass on the very next write, not
// retroactively on construction and not one write() at a time.
TEST_F(SnapshotWriterTest, LoweringTheCapTrimsAnExistingBacklogOnTheNextWrite) {
    {
        palim::SnapshotWriter firstRun(dir);  // unlimited
        for (int i = 0; i < 5; ++i) {
            firstRun.write(makeCommitEvent());
        }
    }

    palim::SnapshotWriter secondRun(dir, /*maxCommits=*/2);
    // Construction alone (findNextAvailableId's own scan) must not prune.
    EXPECT_TRUE(std::filesystem::exists(dir / "001"));

    secondRun.write(makeCommitEvent());  // writes 006, then prunes to the cap

    EXPECT_TRUE(std::filesystem::exists(dir / "006"));
    EXPECT_TRUE(std::filesystem::exists(dir / "005"));
    EXPECT_FALSE(std::filesystem::exists(dir / "004"));
    EXPECT_FALSE(std::filesystem::exists(dir / "001"));
}

// Pruning must delete directories, not just leave an empty shell behind
// -- a pruned commit's before.jpg should not survive as orphaned data.
TEST_F(SnapshotWriterTest, PruningActuallyRemovesTheDirectoryContents) {
    palim::SnapshotWriter writer(dir, /*maxCommits=*/1);
    writer.write(makeCommitEvent());
    ASSERT_TRUE(std::filesystem::exists(dir / "001" / "before.jpg"));

    writer.write(makeCommitEvent());

    EXPECT_FALSE(std::filesystem::exists(dir / "001"));
    EXPECT_FALSE(std::filesystem::exists(dir / "001" / "before.jpg"));
}

// Pruning must not disturb the numbering sequence -- the next write
// after a prune still continues from the true highest ID ever written,
// not from however many directories happen to currently exist.
TEST_F(SnapshotWriterTest, PruningDoesNotResetTheIdSequence) {
    palim::SnapshotWriter writer(dir, /*maxCommits=*/2);
    for (int i = 0; i < 4; ++i) {
        writer.write(makeCommitEvent());
    }
    EXPECT_TRUE(std::filesystem::exists(dir / "003"));
    EXPECT_TRUE(std::filesystem::exists(dir / "004"));

    writer.write(makeCommitEvent());
    EXPECT_TRUE(std::filesystem::exists(dir / "005"));
    EXPECT_FALSE(std::filesystem::exists(dir / "003"));
}

}  // namespace
