#include "git_info.hpp"

#include <array>
#include <cstdio>
#include <optional>
#include <sstream>

namespace palim {

namespace {

std::string rstrip(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

// Runs a shell command and returns its stdout, or nullopt if it exited
// non-zero (including "not a git repo" and "git not found", both of
// which exit non-zero).
std::optional<std::string> runCommand(const std::string& command) {
    std::array<char, 256> buffer{};
    std::string output;

    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    const int status = pclose(pipe);
    if (status != 0) {
        return std::nullopt;
    }
    return output;
}

}  // namespace

std::vector<std::string> parseDirtyFiles(const std::string& porcelainOutput) {
    std::vector<std::string> dirtyFiles;
    std::istringstream lines(porcelainOutput);
    std::string line;
    // Each line is "XY <path>" (or "XY <old> -> <new>" for renames);
    // skip the 2-character status code and the space after it.
    while (std::getline(lines, line)) {
        if (line.size() > 3) {
            dirtyFiles.push_back(rstrip(line.substr(3)));
        }
    }
    return dirtyFiles;
}

GitInfo captureGitInfo() {
    GitInfo info;

    auto head = runCommand("git rev-parse HEAD 2>/dev/null");
    if (!head) {
        return info;  // not inside a git repo, or git isn't installed
    }
    info.commitHash = rstrip(*head);
    info.available = true;

    if (auto status = runCommand("git status --porcelain 2>/dev/null")) {
        info.dirtyFiles = parseDirtyFiles(*status);
    }

    return info;
}

}  // namespace palim
