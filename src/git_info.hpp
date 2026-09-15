#pragma once

#include <string>
#include <vector>

namespace palim {

struct GitInfo {
    bool available = false;
    std::string commitHash;
    std::vector<std::string> dirtyFiles;
};

// Runs `git` as a subprocess in the current working directory. Best-effort:
// returns available=false if the directory isn't inside a git repo, or
// `git` isn't installed -- this is supplementary metadata, not something
// the rest of the pipeline depends on.
GitInfo captureGitInfo();

}  // namespace palim
