#include <iostream>
#include <vector>

#include "commit_metadata.hpp"
#include "timeline_format.hpp"

namespace {
// Shared with any future error path, so usage text can't drift from
// what --help itself prints -- the same reasoning palim's own
// printUsage (main.cpp) already follows.
void printUsage(std::ostream& out) {
    out << "Usage: palim-timeline [commits_dir] [--help]\n";
}
}  // namespace

int main(int argc, char** argv) {
    const palim::TimelineArgs args =
        palim::parseTimelineArgs(std::vector<std::string>(argv + 1, argv + argc));
    if (args.helpRequested) {
        // Requesting help is not an error -- stdout and exit 0, matching
        // how every other command-line tool (and palim's own --help)
        // treats it rather than looking like a failure.
        printUsage(std::cout);
        return 0;
    }

    const std::filesystem::path& commitsDir = args.commitsDir;
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
            std::cout << palim::formatOptionalIntChange("exposure_auto", commits[i].exposureAuto, next.exposureAuto);
            std::cout << palim::formatOptionalIntChange("exposure", commits[i].exposureAbsolute, next.exposureAbsolute);
            std::cout << palim::formatOptionalIntChange("gain", commits[i].gain, next.gain);
            std::cout << palim::formatOptionalIntChange("white_balance_auto", commits[i].whiteBalanceAuto,
                                                          next.whiteBalanceAuto);
            std::cout << palim::formatOptionalIntChange("white_balance", commits[i].whiteBalanceTemperature,
                                                          next.whiteBalanceTemperature);
            std::cout << palim::formatGitChange(commits[i], next);
            std::cout << "        v\n\n";
        }
    }

    return 0;
}
