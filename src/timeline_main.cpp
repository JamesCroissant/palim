#include <iostream>

#include "commit_metadata.hpp"
#include "timeline_format.hpp"

int main(int argc, char** argv) {
    const std::filesystem::path commitsDir = (argc > 1) ? argv[1] : "commits";
    const auto commits = palim::listCommits(commitsDir);

    if (commits.empty()) {
        std::cout << "No commits found in " << commitsDir << "\n";
        return 0;
    }

    std::cout << "Experiment Timeline\n\n";
    for (std::size_t i = 0; i < commits.size(); ++i) {
        std::cout << palim::formatCommit(commits[i]);

        if (i + 1 < commits.size()) {
            const auto& next = commits[i + 1];
            std::cout << "\n        |\n";
            std::cout << palim::formatOptionalIntChange("exposure", commits[i].exposureAbsolute, next.exposureAbsolute);
            std::cout << palim::formatOptionalIntChange("gain", commits[i].gain, next.gain);
            std::cout << palim::formatOptionalIntChange("white_balance", commits[i].whiteBalanceTemperature,
                                                          next.whiteBalanceTemperature);
            std::cout << palim::formatGitChange(commits[i], next);
            std::cout << "        v\n\n";
        }
    }

    return 0;
}
