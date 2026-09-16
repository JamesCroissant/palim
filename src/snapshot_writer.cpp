#include "snapshot_writer.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <opencv2/imgcodecs.hpp>

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

}  // namespace

SnapshotWriter::SnapshotWriter(std::filesystem::path outputDir) : outputDir_(std::move(outputDir)) {
    std::filesystem::create_directories(outputDir_);
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

    const std::string timestamp = formatTimestamp(std::chrono::system_clock::now());
    const double sharpness = computeSharpness(event.after);
    const GitInfo git = captureGitInfo();

    const CameraSettings& cam = event.settings;

    std::ofstream meta(dir / "metadata.json");
    meta << "{\n"
         << "  \"timestamp\": \"" << timestamp << "\",\n"
         << "  \"change_score\": " << event.changeScore << ",\n"
         << "  \"sharpness_score\": " << sharpness << ",\n"
         << "  \"camera\": {\n"
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
