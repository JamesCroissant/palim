#pragma once

#include <filesystem>

#include "state_machine.hpp"

namespace palim {

// Writes each CommitEvent to commits/NNN/{before.jpg,after.jpg}.
// Numbering is sequential per run, starting at 1. Metadata.json is
// added in Step 5 — this only handles the images.
class SnapshotWriter {
public:
    explicit SnapshotWriter(std::filesystem::path outputDir = "commits");

    void write(const CommitEvent& event);

private:
    std::filesystem::path outputDir_;
    int nextId_ = 1;
};

}  // namespace palim
