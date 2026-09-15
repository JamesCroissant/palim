#pragma once

#include <filesystem>

#include "state_machine.hpp"

namespace palim {

// Writes each CommitEvent to commits/NNN/{before.jpg,after.jpg,metadata.json}.
// Numbering is sequential per run, starting at 1.
class SnapshotWriter {
public:
    explicit SnapshotWriter(std::filesystem::path outputDir = "commits");

    void write(const CommitEvent& event);

private:
    static std::string formatTimestamp(std::chrono::system_clock::time_point tp);

    std::filesystem::path outputDir_;
    int nextId_ = 1;
};

}  // namespace palim
