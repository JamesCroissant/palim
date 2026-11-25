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

// `git status --porcelain` doesn't emit a path verbatim if it contains a
// character core considers "unusual" -- a literal double quote or
// backslash, a control character, or (core.quotePath defaults to true)
// any non-ASCII byte. Instead it wraps the whole path in double quotes
// and C-escapes every such byte inside (confirmed directly: a file named
// weird"quote.txt shows up as the 18-character literal `"weird\"quote.txt"`,
// not the real 15-character name). Passing that straight through, as
// this parser used to, means metadata.json and palim-timeline's diff
// output would show the escaped, quoted text instead of the actual
// filename for any dirty file unusual enough to trigger it.
//
// Returns `path` unchanged if it isn't quoted at all (the common case --
// an ordinary filename never gets this treatment). Only unquotes a path
// that is quoted over its *entire* length; a rename line's "<old> -> <new>"
// combined string (see parseDirtyFiles's own comment) only has one side
// quoted at a time, if either, so this intentionally leaves that case as
// unresolved as it already was rather than guessing which half to touch.
std::string unquoteGitPath(const std::string& path) {
    if (path.size() < 2 || path.front() != '"' || path.back() != '"') {
        return path;
    }
    const std::size_t end = path.size() - 1;  // index of the closing quote
    std::string result;
    for (std::size_t i = 1; i < end;) {
        if (path[i] != '\\' || i + 1 >= end) {
            result += path[i];
            ++i;
            continue;
        }
        const char next = path[i + 1];
        switch (next) {
            case '"':
                result += '"';
                i += 2;
                break;
            case '\\':
                result += '\\';
                i += 2;
                break;
            case 'n':
                result += '\n';
                i += 2;
                break;
            case 't':
                result += '\t';
                i += 2;
                break;
            case 'a':
                result += '\a';
                i += 2;
                break;
            case 'b':
                result += '\b';
                i += 2;
                break;
            case 'f':
                result += '\f';
                i += 2;
                break;
            case 'r':
                result += '\r';
                i += 2;
                break;
            case 'v':
                result += '\v';
                i += 2;
                break;
            default:
                // A non-ASCII byte is escaped as three octal digits
                // (\NNN), one escape per raw byte -- the only other
                // escape form core's quoting ever emits.
                if (next >= '0' && next <= '7' && i + 3 < end && path[i + 2] >= '0' &&
                    path[i + 2] <= '7' && path[i + 3] >= '0' && path[i + 3] <= '7') {
                    const int value =
                        (next - '0') * 64 + (path[i + 2] - '0') * 8 + (path[i + 3] - '0');
                    result += static_cast<char>(value);
                    i += 4;
                } else {
                    // Not a recognized escape -- keep the character
                    // literally rather than silently dropping the
                    // backslash or misinterpreting it.
                    result += next;
                    i += 2;
                }
        }
    }
    return result;
}

std::vector<std::string> parseDirtyFiles(const std::string& porcelainOutput) {
    std::vector<std::string> dirtyFiles;
    std::istringstream lines(porcelainOutput);
    std::string line;
    // Each line is "XY <path>" (or "XY <old> -> <new>" for renames);
    // skip the 2-character status code and the space after it.
    while (std::getline(lines, line)) {
        if (line.size() > 3) {
            dirtyFiles.push_back(unquoteGitPath(rstrip(line.substr(3))));
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
