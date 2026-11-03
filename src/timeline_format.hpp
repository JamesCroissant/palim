#pragma once

#include <optional>
#include <string>

#include "commit_metadata.hpp"

namespace palim {

// Formatting for palim-timeline's own output, pulled out of
// timeline_main.cpp as plain string-returning functions (instead of
// printing straight to std::cout) so they're reachable from tests --
// timeline_main.cpp itself has `int main()`, so nothing in it can be
// linked into a test binary without a duplicate-symbol clash, and this
// logic had no test coverage at all before now, not even indirectly.

// Truncates a Git commit hash to 7 characters (or less, if shorter).
std::string formatShortHash(const std::string& hash);

// "        <label>: <before> -> <after>\n", or an empty string if
// before == after (nothing changed, nothing to report). "?" stands in
// for a control this device didn't support (std::nullopt).
std::string formatOptionalIntChange(const std::string& label, std::optional<int> before,
                                     std::optional<int> after);

// The "what changed on the software side?" block between two
// consecutive commits: the Git commit hash moving, and any files that
// became dirty that weren't before (files that went from dirty to
// clean are deliberately not reported -- this answers "what got worse
// since the last commit", not a full symmetric diff). Empty if Git
// wasn't available for either commit, or nothing changed.
std::string formatGitChange(const CommitMetadata& before, const CommitMetadata& after);

// The per-commit summary block: timestamp, change/sharpness scores,
// the changed region (if any), camera settings, and Git state (if
// available).
std::string formatCommit(const CommitMetadata& c);

}  // namespace palim
