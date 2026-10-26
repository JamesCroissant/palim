#pragma once

#include <filesystem>

#include "state_machine.hpp"

namespace palim {

// Writes each CommitEvent to commits/NNN/{before.jpg,after.jpg,metadata.json}.
// Numbering is sequential, resuming after whatever commits/NNN
// directories already exist in outputDir -- restarting palim against a
// directory with earlier commits continues from the next free ID
// instead of starting over at 1 and overwriting them.
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
