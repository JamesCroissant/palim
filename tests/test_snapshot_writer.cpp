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

}  // namespace
