#include "timeline_format.hpp"

#include <algorithm>
#include <sstream>

namespace palim {

std::string formatShortHash(const std::string& hash) {
    return hash.substr(0, std::min<std::size_t>(7, hash.size()));
}

std::string formatOptionalIntChange(const std::string& label, std::optional<int> before,
                                     std::optional<int> after) {
    if (before == after) {
        return "";
    }
    std::ostringstream out;
    out << "        " << label << ": ";
    if (before) {
        out << *before;
    } else {
        out << "?";
    }
    out << " -> ";
    if (after) {
        out << *after;
    } else {
        out << "?";
    }
    out << "\n";
    return out.str();
}

std::string formatGitChange(const CommitMetadata& before, const CommitMetadata& after) {
    if (!before.gitAvailable || !after.gitAvailable) {
        return "";
    }
    if (before.gitCommit == after.gitCommit && before.dirtyFiles == after.dirtyFiles) {
        return "";
    }

    std::ostringstream out;
    if (before.gitCommit != after.gitCommit) {
        out << "        git: " << formatShortHash(before.gitCommit) << " -> "
            << formatShortHash(after.gitCommit) << "\n";
    }

    std::vector<std::string> newlyDirty;
    for (const auto& file : after.dirtyFiles) {
        if (std::find(before.dirtyFiles.begin(), before.dirtyFiles.end(), file) == before.dirtyFiles.end()) {
            newlyDirty.push_back(file);
        }
    }
    if (!newlyDirty.empty()) {
        out << "        newly modified:";
        for (const auto& file : newlyDirty) {
            out << " " << file;
        }
        out << "\n";
    }
    return out.str();
}

std::string formatCommit(const CommitMetadata& c) {
    std::ostringstream out;
    out << "#" << c.id << "  " << c.timestamp << "\n";
    out << "  change: " << c.changeScore << "%   sharpness: " << c.sharpnessScore << "\n";
    if (c.changedRegion) {
        const auto& r = *c.changedRegion;
        out << "  region: (" << r.x << "," << r.y << ") " << r.width << "x" << r.height << "\n";
    }
    out << "  camera: " << c.width << "x" << c.height;
    if (c.exposureAbsolute) {
        out << "  exposure=" << *c.exposureAbsolute;
    }
    if (c.gain) {
        out << "  gain=" << *c.gain;
    }
    if (c.whiteBalanceTemperature) {
        out << "  wb=" << *c.whiteBalanceTemperature << "K";
    }
    out << "\n";
    if (c.gitAvailable) {
        out << "  git: " << formatShortHash(c.gitCommit) << " (" << c.dirtyFiles.size() << " dirty files)\n";
    }
    return out.str();
}

}  // namespace palim
