#include <algorithm>
#include <iostream>

#include "commit_metadata.hpp"

namespace {

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
        const std::string shortHash = c.gitCommit.substr(0, std::min<std::size_t>(7, c.gitCommit.size()));
        std::cout << "  git: " << shortHash << " (" << c.dirtyFiles.size() << " dirty files)\n";
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
            std::cout << "        v\n\n";
        }
    }

    return 0;
}
