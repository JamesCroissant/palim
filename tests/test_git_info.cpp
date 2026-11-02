#include <gtest/gtest.h>

#include <filesystem>

#include "git_info.hpp"

namespace {

// captureGitInfo() runs `git` in the current working directory, so these
// tests change it -- restore it unconditionally via RAII so a failed
// assertion (which doesn't unwind the stack) can't leave later tests
// running from the wrong directory.
class CwdGuard {
public:
    CwdGuard() : original_(std::filesystem::current_path()) {}
    ~CwdGuard() { std::filesystem::current_path(original_); }

private:
    std::filesystem::path original_;
};

TEST(GitInfo, AvailableInsideAGitRepo) {
    CwdGuard guard;
    // This test binary is built from within the palim repo checkout.
    std::filesystem::current_path(PALIM_SOURCE_DIR);

    auto info = palim::captureGitInfo();
    EXPECT_TRUE(info.available);
    EXPECT_EQ(info.commitHash.size(), 40u);  // full SHA-1 hex length
}

TEST(GitInfo, UnavailableOutsideAGitRepo) {
    CwdGuard guard;
    std::filesystem::current_path(std::filesystem::temp_directory_path());

    auto info = palim::captureGitInfo();
    EXPECT_FALSE(info.available);
    EXPECT_TRUE(info.commitHash.empty());
    EXPECT_TRUE(info.dirtyFiles.empty());
}

TEST(ParseDirtyFiles, EmptyOutputYieldsNoFiles) {
    EXPECT_TRUE(palim::parseDirtyFiles("").empty());
}

// Ordinary porcelain lines are "XY <path>" -- a 2-character status code,
// a space, then the path. Modified (" M"), added ("A "), and untracked
// ("??") all share this same shape.
TEST(ParseDirtyFiles, ParsesOrdinaryModifiedAddedAndUntrackedLines) {
    const auto files = palim::parseDirtyFiles(" M src/foo.cpp\nA  src/new_file.cpp\n?? untracked.txt\n");
    EXPECT_EQ(files, (std::vector<std::string>{"src/foo.cpp", "src/new_file.cpp", "untracked.txt"}));
}

// A rename's porcelain line is "XY <old> -> <new>" -- the same 3-
// character skip leaves "<old> -> <new>" as one combined string rather
// than splitting it into two paths; this is what that currently looks
// like, not a claim that it's the only sensible representation.
TEST(ParseDirtyFiles, KeepsARenameLineAsOneCombinedString) {
    const auto files = palim::parseDirtyFiles("R  old_name.cpp -> new_name.cpp\n");
    EXPECT_EQ(files, (std::vector<std::string>{"old_name.cpp -> new_name.cpp"}));
}

// git status --porcelain's lines are LF-terminated on Linux, but a
// trailing \r (CRLF, or output captured through something that
// translates line endings) must not leak into the parsed path.
TEST(ParseDirtyFiles, StripsATrailingCarriageReturn) {
    const auto files = palim::parseDirtyFiles(" M src/foo.cpp\r\n");
    EXPECT_EQ(files, (std::vector<std::string>{"src/foo.cpp"}));
}

// A line with only the 2-character status code and no path at all (or
// too short to have one) must be skipped, not produce a bogus empty or
// truncated entry.
TEST(ParseDirtyFiles, SkipsLinesTooShortToContainAPath) {
    const auto files = palim::parseDirtyFiles(" M\n\nA \n M src/real.cpp\n");
    EXPECT_EQ(files, (std::vector<std::string>{"src/real.cpp"}));
}

}  // namespace
