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

    // Best-effort, the same way AppendSessionSummary is in this account's
    // other project (focus-monitor): a commit failing to write (a full
    // disk, a permissions problem, a stray file occupying the exact
    // "NNN" path a future commit ID needs) must never crash the whole
    // capture/processing/storage pipeline over one bad write -- there are
    // still two live camera/processing threads whose work this would
    // otherwise throw away too. Returns false (instead of throwing or
    // silently claiming success) if any of before.jpg/after.jpg/
    // after_annotated.jpg/metadata.json failed to write, so the caller
    // can report it rather than announcing a save that didn't actually
    // happen.
    bool write(const CommitEvent& event);

private:
    static std::string formatTimestamp(std::chrono::system_clock::time_point tp);
    // Writes dir/metadata.json. Returns whether the ofstream came out of
    // it still good -- the one write() step with no cv::imwrite bool to
    // check instead.
    static bool writeMetadata(const std::filesystem::path& dir, const CommitEvent& event);
    void pruneOldCommits();

    std::filesystem::path outputDir_;
    int nextId_ = 1;
    int maxCommits_ = 0;
};

}  // namespace palim
