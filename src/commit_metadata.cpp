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

// std::stoi/std::stod throw on a value that isn't a parseable number at
// all -- reachable not just from a hand-edited file, but from a write
// torn mid-field (the process killed, or the disk full, between writing
// a key's colon and its value): extractRawValue still finds the key and
// returns whatever (possibly empty) text sits before the next
// ','/'\n'/'}', so the raw/null checks below never catch it, and the
// stoi/stod call sees something like "" instead. Every other way this
// function already tolerates a bad document -- a missing key, an
// explicit "null", a missing "changed_region" -- returns cleanly rather
// than throwing; an unparseable-but-present value is treated the same
// way here, not as a reason to crash whatever's reading this document
// (readCommitMetadata, and through it palim-timeline's whole listing).
std::optional<int> extractOptionalInt(const std::string& json, const std::string& key) {
    auto raw = extractRawValue(json, key);
    if (!raw || *raw == "null") {
        return std::nullopt;
    }
    try {
        return std::stoi(*raw);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

double extractDouble(const std::string& json, const std::string& key) {
    auto raw = extractRawValue(json, key);
    if (!raw) {
        return 0.0;
    }
    try {
        return std::stod(*raw);
    } catch (const std::exception&) {
        return 0.0;
    }
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

namespace {

// SnapshotWriter::write() zero-pads a commit's numeric ID to 3 digits
// (std::setw(3)), but that only sets a *minimum* width -- it doesn't
// truncate or widen the padding once an ID needs more digits than that,
// so directory "1000" is 4 characters, not "1000" re-padded to stay
// ahead of "999". A long-running, unattended session (exactly the use
// case --max-commits exists for) can genuinely reach a 4-digit commit
// count, and from there plain lexicographic path comparison gets the
// order wrong: "1000" sorts *before* "999" as a string ('1' < '9'),
// even though 1000 > 999 as a commit number. Parsing each directory
// name as the integer ID it actually is (the same way
// SnapshotWriter's own listCommitDirs() already does, for the same
// reason) and sorting by that instead makes the order correct no
// matter how many digits a commit ID ever grows to -- not just safe up
// to some larger but still eventually-reachable padding width.
std::optional<int> parseCommitId(const std::filesystem::path& dir) {
    const std::string name = dir.filename().string();
    if (name.empty() || !std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }
    try {
        return std::stoi(name);
    } catch (const std::exception&) {
        return std::nullopt;  // e.g. a numeric name too long for int
    }
}

}  // namespace

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
    // Directories outside SnapshotWriter's own "NNN" naming (a stray
    // file, something a human created) have no parsed ID -- fall back
    // to comparing those by path so the sort stays a strict ordering,
    // but every real commit directory sorts by its actual numeric ID.
    // Directories outside SnapshotWriter's own "NNN" naming (a stray
    // file, something a human created) have no parsed ID -- fall back
    // to comparing those by path so the sort stays a strict ordering,
    // but every real commit directory sorts by its actual numeric ID.
    std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
        const auto idA = parseCommitId(a);
        const auto idB = parseCommitId(b);
        if (idA && idB) {
            return *idA < *idB;
        }
        return a < b;
    });

    for (const auto& dir : dirs) {
        if (auto m = readCommitMetadata(dir)) {
            result.push_back(*m);
        }
    }
    return result;
}

}  // namespace palim
