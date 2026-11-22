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

// True if directory `a` should sort before directory `b` when listing
// commits: by numeric ID when both parse as one (SnapshotWriter's own
// "NNN" naming, any digit width), with a numeric-ID directory always
// ordered before a non-numeric one (a stray entry that isn't a real
// commit directory), and by plain path otherwise. Exposed here (rather
// than kept as a lambda local to listCommits below) specifically so
// this ordering -- the one part of listCommits that has to satisfy
// std::sort's own strict-weak-ordering requirement for its comparator
// -- can be checked directly.
bool commitDirectoryLessThan(const std::filesystem::path& a, const std::filesystem::path& b);

// Reads every commits/NNN/ subdirectory that has a metadata.json,
// sorted by each directory's actual numeric commit ID (see
// commitDirectoryLessThan above) rather than by directory name --
// zero-padding alone doesn't stay ahead of an ID that grows past its
// padding width (see SnapshotWriter::write()'s own std::setw(3)).
std::vector<CommitMetadata> listCommits(const std::filesystem::path& commitsDir);

}  // namespace palim
