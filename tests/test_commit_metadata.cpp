#include <gtest/gtest.h>

#include <fstream>

#include "commit_metadata.hpp"

namespace {

class CommitMetadataTest : public ::testing::Test {
protected:
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "palim_test_commit_metadata";

    void SetUp() override { std::filesystem::create_directories(dir); }
    void TearDown() override { std::filesystem::remove_all(dir); }

    void writeMetadata(const std::string& json) {
        std::ofstream file(dir / "metadata.json");
        file << json;
    }
};

TEST_F(CommitMetadataTest, ParsesAFullyPopulatedDocument) {
    writeMetadata(R"({
  "timestamp": "2026-09-16T00:52:13",
  "change_score": 42.5,
  "sharpness_score": 1201.63,
  "camera": {
    "width": 1280,
    "height": 720,
    "exposure_auto": 1,
    "exposure_absolute": 220,
    "gain": 32,
    "white_balance_auto": 1,
    "white_balance_temperature": 4600
  },
  "git": {
    "available": true,
    "commit": "a91e32f1234567890",
    "dirty_files": [
      "src/capture.cpp",
      "src/buffer.cpp"
    ]
  }
})");

    auto m = palim::readCommitMetadata(dir);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->timestamp, "2026-09-16T00:52:13");
    EXPECT_DOUBLE_EQ(m->changeScore, 42.5);
    EXPECT_DOUBLE_EQ(m->sharpnessScore, 1201.63);
    EXPECT_EQ(m->width, 1280);
    EXPECT_EQ(m->height, 720);
    ASSERT_TRUE(m->exposureAbsolute.has_value());
    EXPECT_EQ(*m->exposureAbsolute, 220);
    ASSERT_TRUE(m->gain.has_value());
    EXPECT_EQ(*m->gain, 32);
    ASSERT_TRUE(m->whiteBalanceTemperature.has_value());
    EXPECT_EQ(*m->whiteBalanceTemperature, 4600);
    EXPECT_TRUE(m->gitAvailable);
    EXPECT_EQ(m->gitCommit, "a91e32f1234567890");
    EXPECT_EQ(m->dirtyFiles, (std::vector<std::string>{"src/capture.cpp", "src/buffer.cpp"}));
}

TEST_F(CommitMetadataTest, ParsesAChangedRegion) {
    writeMetadata(R"({
  "timestamp": "2026-09-16T00:52:13",
  "change_score": 42.5,
  "sharpness_score": 1201.63,
  "changed_region": {
    "region_x": 120,
    "region_y": 45,
    "region_w": 300,
    "region_h": 220
  },
  "camera": {
    "width": 1280,
    "height": 720,
    "exposure_auto": null,
    "exposure_absolute": null,
    "gain": null,
    "white_balance_auto": null,
    "white_balance_temperature": null
  },
  "git": {
    "available": false
  }
})");

    auto m = palim::readCommitMetadata(dir);
    ASSERT_TRUE(m.has_value());
    ASSERT_TRUE(m->changedRegion.has_value());
    EXPECT_EQ(m->changedRegion->x, 120);
    EXPECT_EQ(m->changedRegion->y, 45);
    EXPECT_EQ(m->changedRegion->width, 300);
    EXPECT_EQ(m->changedRegion->height, 220);
    // The region's own x/y/w/h must not have been confused with the
    // "camera" object's unrelated width/height -- exactly the collision
    // SnapshotWriter's region_* naming exists to avoid.
    EXPECT_EQ(m->width, 1280);
    EXPECT_EQ(m->height, 720);
}

TEST_F(CommitMetadataTest, NullChangedRegionBecomesNullopt) {
    writeMetadata(R"({
  "timestamp": "2026-09-16T00:52:13",
  "change_score": 0,
  "sharpness_score": 0,
  "changed_region": null,
  "camera": {
    "width": 640, "height": 480, "exposure_auto": null, "exposure_absolute": null,
    "gain": null, "white_balance_auto": null, "white_balance_temperature": null
  },
  "git": {"available": false}
})");

    auto m = palim::readCommitMetadata(dir);
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->changedRegion.has_value());
}

// Older metadata.json files (written before this field existed) have no
// "changed_region" key at all -- has to parse as "no region", not crash
// or throw.
TEST_F(CommitMetadataTest, MissingChangedRegionKeyBecomesNullopt) {
    writeMetadata(R"({
  "timestamp": "2026-09-16T00:52:13",
  "change_score": 0,
  "sharpness_score": 0,
  "camera": {
    "width": 640, "height": 480, "exposure_auto": null, "exposure_absolute": null,
    "gain": null, "white_balance_auto": null, "white_balance_temperature": null
  },
  "git": {"available": false}
})");

    auto m = palim::readCommitMetadata(dir);
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->changedRegion.has_value());
}

TEST_F(CommitMetadataTest, NullCameraControlsBecomeNullopt) {
    writeMetadata(R"({
  "timestamp": "2026-09-16T00:00:00",
  "change_score": 10,
  "sharpness_score": 0,
  "camera": {
    "width": 640,
    "height": 480,
    "exposure_auto": null,
    "exposure_absolute": null,
    "gain": null,
    "white_balance_auto": null,
    "white_balance_temperature": null
  },
  "git": {
    "available": false
  }
})");

    auto m = palim::readCommitMetadata(dir);
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->exposureAuto.has_value());
    EXPECT_FALSE(m->exposureAbsolute.has_value());
    EXPECT_FALSE(m->gain.has_value());
    EXPECT_FALSE(m->whiteBalanceAuto.has_value());
    EXPECT_FALSE(m->whiteBalanceTemperature.has_value());
    EXPECT_FALSE(m->gitAvailable);
    EXPECT_TRUE(m->dirtyFiles.empty());
}

// A write torn mid-field (process killed, or disk full, between a
// key's colon and its value) leaves extractRawValue() finding the key
// but capturing empty or garbage text before the next ','/'\n'/'}' --
// neither missing nor an explicit "null", so the usual nullopt checks
// don't catch it, and std::stoi/std::stod would otherwise throw
// std::invalid_argument straight out of readCommitMetadata() (and
// crash palim-timeline's whole listing over one bad commit). Confirmed
// this was a real, reachable crash before the fix: building this exact
// document and calling readCommitMetadata() directly terminated the
// process via an uncaught std::invalid_argument from std::stoi.
TEST_F(CommitMetadataTest, FieldTornMidWriteParsesAsMissingInsteadOfThrowing) {
    writeMetadata(R"({
  "timestamp": "2026-09-16T00:52:13",
  "change_score": 42.5,
  "sharpness_score": 1201.63,
  "camera": {
    "width": 1280,
    "height": 720,
    "exposure_auto":,
    "exposure_absolute": 220,
    "gain": 32,
    "white_balance_auto": 1,
    "white_balance_temperature": 4600
  },
  "git": {"available": false}
})");

    auto m = palim::readCommitMetadata(dir);
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->exposureAuto.has_value());
    // The fields on either side of the torn one parsed normally --
    // this is about tolerating the one bad field, not losing the whole
    // document around it.
    EXPECT_EQ(m->width, 1280);
    EXPECT_TRUE(m->exposureAbsolute.has_value());
    EXPECT_EQ(*m->exposureAbsolute, 220);
}

// SnapshotWriter's jsonEscape() writes a literal trailing backslash as
// two backslashes ("\\" in the JSON source). The old escape-skip logic
// only looked at the single character right before a candidate closing
// quote, so it saw that second backslash and wrongly concluded the real
// closing quote right after it was itself escaped -- sending the search
// past the string's true end and corrupting whatever came after it in
// the array. Counting the full run of backslashes (even = not escaped)
// gets this right, and a second array entry confirms parsing resumed
// correctly instead of losing its place.
TEST_F(CommitMetadataTest, DirtyFileEndingInLiteralBackslashDoesNotCorruptParsing) {
    writeMetadata(R"({
  "timestamp": "2026-09-16T00:52:13",
  "change_score": 0,
  "sharpness_score": 0,
  "camera": {
    "width": 640, "height": 480, "exposure_auto": null, "exposure_absolute": null,
    "gain": null, "white_balance_auto": null, "white_balance_temperature": null
  },
  "git": {
    "available": true,
    "commit": "a91e32f1234567890",
    "dirty_files": [
      "weird\\",
      "src/capture.cpp"
    ]
  }
})");

    auto m = palim::readCommitMetadata(dir);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->dirtyFiles, (std::vector<std::string>{"weird\\", "src/capture.cpp"}));
    EXPECT_EQ(m->gitCommit, "a91e32f1234567890");
}

// A dirty file path containing a literal double quote is written by
// jsonEscape() as \" in the JSON. The parsed value must come back with
// the quote restored rather than left as a literal backslash-quote pair.
TEST_F(CommitMetadataTest, DirtyFileWithEmbeddedQuoteIsUnescaped) {
    writeMetadata(R"({
  "timestamp": "2026-09-16T00:52:13",
  "change_score": 0,
  "sharpness_score": 0,
  "camera": {
    "width": 640, "height": 480, "exposure_auto": null, "exposure_absolute": null,
    "gain": null, "white_balance_auto": null, "white_balance_temperature": null
  },
  "git": {
    "available": true,
    "commit": "deadbeef",
    "dirty_files": [
      "say \"hi\".txt"
    ]
  }
})");

    auto m = palim::readCommitMetadata(dir);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->dirtyFiles, (std::vector<std::string>{"say \"hi\".txt"}));
}

TEST_F(CommitMetadataTest, MissingFileReturnsNullopt) {
    // dir exists but metadata.json was never written.
    EXPECT_FALSE(palim::readCommitMetadata(dir).has_value());
}

TEST(ListCommits, ReturnsEmptyForMissingDirectory) {
    EXPECT_TRUE(palim::listCommits("/nonexistent/path/for/palim/tests").empty());
}

TEST(ListCommits, ReturnsCommitsInNumericOrder) {
    const std::filesystem::path root = std::filesystem::temp_directory_path() / "palim_test_list_commits";
    std::filesystem::remove_all(root);
    for (const std::string& id : {"003", "001", "002"}) {
        std::filesystem::create_directories(root / id);
        std::ofstream file(root / id / "metadata.json");
        file << R"({"timestamp": ")" << id << R"(", "change_score": 0, "sharpness_score": 0,
                "camera": {"width": 0, "height": 0, "exposure_auto": null, "exposure_absolute": null,
                           "gain": null, "white_balance_auto": null, "white_balance_temperature": null},
                "git": {"available": false}})";
    }

    auto commits = palim::listCommits(root);
    ASSERT_EQ(commits.size(), 3u);
    EXPECT_EQ(commits[0].id, "001");
    EXPECT_EQ(commits[1].id, "002");
    EXPECT_EQ(commits[2].id, "003");

    std::filesystem::remove_all(root);
}

// SnapshotWriter::write() zero-pads a commit's numeric ID with
// std::setw(3), which only sets a *minimum* width: once a long-running
// session reaches a 4-digit ID, "1000" is 4 characters, not "1000"
// padded any wider than that. ReturnsCommitsInNumericOrder above never
// exercises this since every ID there is the same width -- plain
// lexicographic path comparison happens to get same-width zero-padded
// names right, which is exactly why this gap went unnoticed. "1000"
// sorts *before* "999" as a string ('1' < '9'), even though 1000 > 999
// as a commit number, so this pins down that mixed-width IDs still
// come back in the right numeric order.
TEST(ListCommits, ReturnsCommitsInNumericOrderAcrossDifferentIdWidths) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "palim_test_list_commits_mixed_width";
    std::filesystem::remove_all(root);
    for (const std::string& id : {"1001", "998", "1000", "999"}) {
        std::filesystem::create_directories(root / id);
        std::ofstream file(root / id / "metadata.json");
        file << R"({"timestamp": ")" << id << R"(", "change_score": 0, "sharpness_score": 0,
                "camera": {"width": 0, "height": 0, "exposure_auto": null, "exposure_absolute": null,
                           "gain": null, "white_balance_auto": null, "white_balance_temperature": null},
                "git": {"available": false}})";
    }

    auto commits = palim::listCommits(root);
    ASSERT_EQ(commits.size(), 4u);
    EXPECT_EQ(commits[0].id, "998");
    EXPECT_EQ(commits[1].id, "999");
    EXPECT_EQ(commits[2].id, "1000");
    EXPECT_EQ(commits[3].id, "1001");

    std::filesystem::remove_all(root);
}

}  // namespace
