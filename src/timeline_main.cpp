#include <algorithm>
#include <iostream>

#include "commit_metadata.hpp"

namespace {

std::string shortHash(const std::string& hash) {
    return hash.substr(0, std::min<std::size_t>(7, hash.size()));
}

void printOptionalIntChange(const std::string& label, const std::optional<int>& before,
                             const std::optional<int>& after) {
    if (before == after) {
        return;
    }
    std::cout << "        " << label << ": ";
    if (before) {
        std::cout << *before;
    } else {
        std::cout << "?";
    }
    std::cout << " -> ";
    if (after) {
        std::cout << *after;
    } else {
        std::cout << "?";
    }
    std::cout << "\n";
}

// "What changed?" (spec section 4) for the software side: did the Git
// commit move, and which files became dirty that weren't before. This
// only compares the two metadata.json snapshots already on disk -- no
// git commands are re-run here, and there's no attempt to guess *why*
// something changed (that's future work, deliberately not attempted
// yet).
void printGitChange(const palim::CommitMetadata& before, const palim::CommitMetadata& after) {
    if (!before.gitAvailable || !after.gitAvailable) {
        return;
    }
    if (before.gitCommit == after.gitCommit && before.dirtyFiles == after.dirtyFiles) {
        return;
    }

    if (before.gitCommit != after.gitCommit) {
        std::cout << "        git: " << shortHash(before.gitCommit) << " -> " << shortHash(after.gitCommit) << "\n";
    }

    std::vector<std::string> newlyDirty;
    for (const auto& file : after.dirtyFiles) {
        if (std::find(before.dirtyFiles.begin(), before.dirtyFiles.end(), file) == before.dirtyFiles.end()) {
            newlyDirty.push_back(file);
        }
    }
    if (!newlyDirty.empty()) {
        std::cout << "        newly modified:";
        for (const auto& file : newlyDirty) {
            std::cout << " " << file;
        }
        std::cout << "\n";
    }
}

void printCommit(const palim::CommitMetadata& c) {
    std::cout << "#" << c.id << "  " << c.timestamp << "\n";
    std::cout << "  change: " << c.changeScore << "%   sharpness: " << c.sharpnessScore << "\n";
    std::cout << "  camera: " << c.width << "x" << c.height;
    if (c.exposureAbsolute) {
        std::cout << "  exposure=" << *c.exposureAbsolute;
    }
    if (c.gain) {
        std::cout << "  gain=" << *c.gain;
    }
    if (c.whiteBalanceTemperature) {
        std::cout << "  wb=" << *c.whiteBalanceTemperature << "K";
    }
    std::cout << "\n";
    if (c.gitAvailable) {
        std::cout << "  git: " << shortHash(c.gitCommit) << " (" << c.dirtyFiles.size() << " dirty files)\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path commitsDir = (argc > 1) ? argv[1] : "commits";
    const auto commits = palim::listCommits(commitsDir);

    if (commits.empty()) {
        std::cout << "No commits found in " << commitsDir << "\n";
        return 0;
    }

    std::cout << "Experiment Timeline\n\n";
    for (std::size_t i = 0; i < commits.size(); ++i) {
        printCommit(commits[i]);

        if (i + 1 < commits.size()) {
            const auto& next = commits[i + 1];
            std::cout << "\n        |\n";
            printOptionalIntChange("exposure", commits[i].exposureAbsolute, next.exposureAbsolute);
            printOptionalIntChange("gain", commits[i].gain, next.gain);
            printOptionalIntChange("white_balance", commits[i].whiteBalanceTemperature, next.whiteBalanceTemperature);
            printGitChange(commits[i], next);
            std::cout << "        v\n\n";
        }
    }

    return 0;
}
