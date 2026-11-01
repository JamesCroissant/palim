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
    // maxCommits caps how many commits/NNN directories outputDir ever
    // holds at once: after each write(), the oldest (lowest-numbered)
    // ones are deleted until back at the cap. 0 (the default) means
    // unlimited -- palim left running unattended for days otherwise has
    // no way to stop filling the disk with before/after JPEG pairs.
    explicit SnapshotWriter(std::filesystem::path outputDir = "commits", int maxCommits = 0);

    void write(const CommitEvent& event);

private:
    static std::string formatTimestamp(std::chrono::system_clock::time_point tp);
    void pruneOldCommits();

    std::filesystem::path outputDir_;
    int nextId_ = 1;
    int maxCommits_ = 0;
};

}  // namespace palim
