#include "snapshot_writer.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "frame_quality.hpp"
#include "git_info.hpp"

namespace palim {

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

// Renders the control's value, or the JSON literal null if this device
// doesn't support it.
std::string jsonOptionalInt(std::optional<int> value) {
    return value ? std::to_string(*value) : "null";
}

// Scans outputDir for subdirectories this class has already written
// (named "NNN", all digits -- anything else, a stray file or a
// directory a human created, is ignored rather than treated as a parse
// error) and returns one past the highest ID found, or 1 if none exist.
// Restarting palim against a directory with existing commits must
// resume numbering from there, not restart at 1 and silently overwrite
// them -- the exact failure mode this function exists to prevent.
int findNextAvailableId(const std::filesystem::path& outputDir) {
    int maxId = 0;
    if (!std::filesystem::exists(outputDir)) {
        return 1;
    }
    for (const auto& entry : std::filesystem::directory_iterator(outputDir)) {
        if (!entry.is_directory()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        const bool allDigits =
            !name.empty() &&
            std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
        if (!allDigits) {
            continue;
        }
        try {
            maxId = std::max(maxId, std::stoi(name));
        } catch (const std::exception&) {
            continue;  // e.g. a numeric name too long for int -- skip it
        }
    }
    return maxId + 1;
}

}  // namespace

SnapshotWriter::SnapshotWriter(std::filesystem::path outputDir) : outputDir_(std::move(outputDir)) {
    std::filesystem::create_directories(outputDir_);
    nextId_ = findNextAvailableId(outputDir_);
}

std::string SnapshotWriter::formatTimestamp(std::chrono::system_clock::time_point tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
    localtime_r(&t, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

void SnapshotWriter::write(const CommitEvent& event) {
    std::ostringstream id;
    id << std::setw(3) << std::setfill('0') << nextId_;

    const std::filesystem::path dir = outputDir_ / id.str();
    std::filesystem::create_directories(dir);

    cv::imwrite((dir / "before.jpg").string(), event.before);
    cv::imwrite((dir / "after.jpg").string(), event.after);

    // A visible answer to "where did it change?" alongside the metadata
    // one: a copy of `after` with the changed region boxed, rather than
    // drawing on `after.jpg` itself -- computeSharpness() below and any
    // future consumer of the raw frame should still see exactly what the
    // camera captured, not an annotated version of it.
    if (event.changedRegion) {
        cv::Mat annotated = event.after.clone();
        cv::rectangle(annotated, *event.changedRegion, cv::Scalar(0, 0, 255), 2);
        cv::imwrite((dir / "after_annotated.jpg").string(), annotated);
    }

    const std::string timestamp = formatTimestamp(std::chrono::system_clock::now());
    const double sharpness = computeSharpness(event.after);
    const GitInfo git = captureGitInfo();

    const CameraSettings& cam = event.settings;

    std::ofstream meta(dir / "metadata.json");
    meta << "{\n"
         << "  \"timestamp\": \"" << timestamp << "\",\n"
         << "  \"change_score\": " << event.changeScore << ",\n"
         << "  \"sharpness_score\": " << sharpness << ",\n";
    // region_x/y/w/h, not x/y/width/height: the "camera" object below
    // already owns "width"/"height" as unique top-level key names, and
    // commit_metadata.cpp's reader finds a key anywhere in the whole
    // document rather than tracking which nested object it's in -- see
    // its own comment. Reusing "width"/"height" here would make it read
    // back the wrong one.
    if (event.changedRegion) {
        const cv::Rect& r = *event.changedRegion;
        meta << "  \"changed_region\": {\n"
             << "    \"region_x\": " << r.x << ",\n"
             << "    \"region_y\": " << r.y << ",\n"
             << "    \"region_w\": " << r.width << ",\n"
             << "    \"region_h\": " << r.height << "\n"
             << "  },\n";
    } else {
        meta << "  \"changed_region\": null,\n";
    }
    meta << "  \"camera\": {\n"
         << "    \"width\": " << cam.width << ",\n"
         << "    \"height\": " << cam.height << ",\n"
         << "    \"exposure_auto\": " << jsonOptionalInt(cam.exposureAuto) << ",\n"
         << "    \"exposure_absolute\": " << jsonOptionalInt(cam.exposureAbsolute) << ",\n"
         << "    \"gain\": " << jsonOptionalInt(cam.gain) << ",\n"
         << "    \"white_balance_auto\": " << jsonOptionalInt(cam.whiteBalanceAuto) << ",\n"
         << "    \"white_balance_temperature\": " << jsonOptionalInt(cam.whiteBalanceTemperature) << "\n"
         << "  },\n"
         << "  \"git\": {\n"
         << "    \"available\": " << (git.available ? "true" : "false") << (git.available ? ",\n" : "\n");
    if (git.available) {
        meta << "    \"commit\": \"" << jsonEscape(git.commitHash) << "\",\n"
             << "    \"dirty_files\": [";
        for (std::size_t i = 0; i < git.dirtyFiles.size(); ++i) {
            meta << (i == 0 ? "\n" : ",\n") << "      \"" << jsonEscape(git.dirtyFiles[i]) << "\"";
        }
        meta << (git.dirtyFiles.empty() ? "" : "\n") << "    ]\n";
    }
    meta << "  }\n"
         << "}\n";

    ++nextId_;
}

}  // namespace palim
