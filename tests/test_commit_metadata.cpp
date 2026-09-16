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

}  // namespace
