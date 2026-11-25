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

// Undoes the C-style double-quoting `git status --porcelain` wraps a
// path in whenever it contains a character core considers "unusual"
// (a literal double quote or backslash, a control character, or --
// since core.quotePath defaults to true -- any non-ASCII byte, each
// escaped as \NNN octal). Returns `path` unchanged if it isn't quoted
// at all. Exported (not file-local, like parseDirtyFiles's own helpers)
// specifically so this escaping logic -- the one part of parsing a
// dirty-file line that can silently mangle a real filename rather than
// just mis-splitting a line -- is directly testable on its own.
std::string unquoteGitPath(const std::string& path);

}  // namespace palim
