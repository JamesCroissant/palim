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

}  // namespace
