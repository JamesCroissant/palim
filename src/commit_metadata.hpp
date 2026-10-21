#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace palim {

// Mirrors SnapshotWriter's "changed_region" object. Deliberately a plain
// struct, not cv::Rect: commit_metadata.hpp/.cpp (and palim-timeline,
// which links against them) have no OpenCV dependency, and this is the
// only field that would otherwise need one.
struct ChangedRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Mirrors exactly what SnapshotWriter writes to metadata.json. Parsed by
// commit_metadata.cpp's small, purpose-built reader -- not a general
// JSON parser, just enough string-searching to read our own fixed
// schema back out.
struct CommitMetadata {
    std::string id;  // the commits/NNN directory name
    std::string timestamp;
    double changeScore = 0.0;
    double sharpnessScore = 0.0;
    std::optional<ChangedRegion> changedRegion;

    int width = 0;
    int height = 0;
    std::optional<int> exposureAuto;
    std::optional<int> exposureAbsolute;
    std::optional<int> gain;
    std::optional<int> whiteBalanceAuto;
    std::optional<int> whiteBalanceTemperature;

    bool gitAvailable = false;
    std::string gitCommit;
    std::vector<std::string> dirtyFiles;
};

// Reads commits/NNN/metadata.json. Returns nullopt if the file is
// missing or unreadable.
std::optional<CommitMetadata> readCommitMetadata(const std::filesystem::path& commitDir);

// Reads every commits/NNN/ subdirectory that has a metadata.json,
// sorted by directory name (zero-padded, so lexicographic == numeric
// order).
std::vector<CommitMetadata> listCommits(const std::filesystem::path& commitsDir);

}  // namespace palim
