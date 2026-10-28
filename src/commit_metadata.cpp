#include "commit_metadata.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace palim {

namespace {

// Finds the closing quote for a JSON string whose opening quote is at
// s[openQuotePos], honoring backslash escapes correctly: a quote
// preceded by an *odd* number of consecutive backslashes is itself
// escaped (part of the string's content), not the closing delimiter.
// Checking only the single preceding character -- which is what this
// used to do -- gets that wrong for a value containing a literal
// backslash immediately before a real delimiter: a dirty file path
// ending in one literal "\" is written as "\\" by SnapshotWriter's
// jsonEscape(), and a naive single-character check sees that trailing
// backslash and wrongly concludes the quote right after it is escaped,
// sending the search past the string's real end.
std::optional<std::size_t> findClosingQuote(const std::string& s, std::size_t openQuotePos) {
    std::size_t pos = openQuotePos + 1;
    while (true) {
        pos = s.find('"', pos);
        if (pos == std::string::npos) {
            return std::nullopt;
        }
        std::size_t backslashCount = 0;
        for (std::size_t i = pos; i > 0 && s[i - 1] == '\\'; --i) {
            ++backslashCount;
        }
        if (backslashCount % 2 == 0) {
            return pos;
        }
        ++pos;  // this quote is escaped; keep searching after it
    }
}

// Reverses SnapshotWriter's jsonEscape(): turns \" and \\ back into "
// and \, the only two escapes it ever writes, so a round-tripped string
// value comes back exactly as it went in.
std::string jsonUnescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == '"' || s[i + 1] == '\\')) {
            out += s[i + 1];
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Finds "key": <value> and returns <value> (quotes stripped and
// escapes reversed for string values, otherwise the raw token up to
// the next , \n or }). Works here only because every key SnapshotWriter
// emits is unique across the whole document -- there's no need to
// track which object (top-level, "camera", "git") a key belongs to.
std::optional<std::string> extractRawValue(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos += needle.size();
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size()) {
        return std::nullopt;
    }

    if (json[pos] == '"') {
        const auto end = findClosingQuote(json, pos);
        if (!end) {
            return std::nullopt;
        }
        return jsonUnescape(json.substr(pos + 1, *end - pos - 1));
    }

    auto end = json.find_first_of(",\n}", pos);
    if (end == std::string::npos) {
        end = json.size();
    }
    std::string value = json.substr(pos, end - pos);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::optional<int> extractOptionalInt(const std::string& json, const std::string& key) {
    auto raw = extractRawValue(json, key);
    if (!raw || *raw == "null") {
        return std::nullopt;
    }
    return std::stoi(*raw);
}

double extractDouble(const std::string& json, const std::string& key) {
    auto raw = extractRawValue(json, key);
    return raw ? std::stod(*raw) : 0.0;
}

std::optional<ChangedRegion> extractChangedRegion(const std::string& json) {
    // All four or none: "changed_region" is written as either a whole
    // object or the single token `null` (see SnapshotWriter), never
    // partially populated.
    const auto x = extractOptionalInt(json, "region_x");
    const auto y = extractOptionalInt(json, "region_y");
    const auto w = extractOptionalInt(json, "region_w");
    const auto h = extractOptionalInt(json, "region_h");
    if (!x || !y || !w || !h) {
        return std::nullopt;
    }
    return ChangedRegion{*x, *y, *w, *h};
}

std::vector<std::string> extractStringArray(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    const std::string needle = "\"" + key + "\":";
    auto keyPos = json.find(needle);
    if (keyPos == std::string::npos) {
        return result;
    }
    auto arrayStart = json.find('[', keyPos);
    auto arrayEnd = json.find(']', arrayStart);
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos) {
        return result;
    }

    const std::string content = json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
    std::size_t i = 0;
    while (i < content.size()) {
        auto open = content.find('"', i);
        if (open == std::string::npos) {
            break;
        }
        const auto close = findClosingQuote(content, open);
        if (!close) {
            break;
        }
        result.push_back(jsonUnescape(content.substr(open + 1, *close - open - 1)));
        i = *close + 1;
    }
    return result;
}

}  // namespace

std::optional<CommitMetadata> readCommitMetadata(const std::filesystem::path& commitDir) {
    std::ifstream file(commitDir / "metadata.json");
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string json = buffer.str();

    CommitMetadata m;
    m.id = commitDir.filename().string();
    m.timestamp = extractRawValue(json, "timestamp").value_or("");
    m.changeScore = extractDouble(json, "change_score");
    m.sharpnessScore = extractDouble(json, "sharpness_score");
    m.changedRegion = extractChangedRegion(json);
    m.width = static_cast<int>(extractDouble(json, "width"));
    m.height = static_cast<int>(extractDouble(json, "height"));
    m.exposureAuto = extractOptionalInt(json, "exposure_auto");
    m.exposureAbsolute = extractOptionalInt(json, "exposure_absolute");
    m.gain = extractOptionalInt(json, "gain");
    m.whiteBalanceAuto = extractOptionalInt(json, "white_balance_auto");
    m.whiteBalanceTemperature = extractOptionalInt(json, "white_balance_temperature");
    m.gitAvailable = extractRawValue(json, "available").value_or("false") == "true";
    m.gitCommit = extractRawValue(json, "commit").value_or("");
    m.dirtyFiles = extractStringArray(json, "dirty_files");
    return m;
}

std::vector<CommitMetadata> listCommits(const std::filesystem::path& commitsDir) {
    std::vector<CommitMetadata> result;
    if (!std::filesystem::exists(commitsDir)) {
        return result;
    }

    std::vector<std::filesystem::path> dirs;
    for (const auto& entry : std::filesystem::directory_iterator(commitsDir)) {
        if (entry.is_directory()) {
            dirs.push_back(entry.path());
        }
    }
    std::sort(dirs.begin(), dirs.end());  // zero-padded names -> lexicographic == numeric

    for (const auto& dir : dirs) {
        if (auto m = readCommitMetadata(dir)) {
            result.push_back(*m);
        }
    }
    return result;
}

}  // namespace palim
