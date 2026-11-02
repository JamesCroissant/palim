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

// Parses `git status --porcelain`'s output into the list of affected
// paths: each line is "XY <path>" (or "XY <old> -> <new>" for a rename,
// kept as one combined string rather than split into two -- see this
// function's own tests for exactly what that looks like), so skipping
// the 2-character status code and the space after it leaves just the
// path. Pulled out of captureGitInfo() so this parsing can be
// unit-tested against synthetic porcelain text, without a real git
// subprocess or repository.
std::vector<std::string> parseDirtyFiles(const std::string& porcelainOutput);

}  // namespace palim
