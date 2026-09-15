#include "snapshot_writer.hpp"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace palim {

SnapshotWriter::SnapshotWriter(std::filesystem::path outputDir) : outputDir_(std::move(outputDir)) {
    std::filesystem::create_directories(outputDir_);
}

double SnapshotWriter::computeSharpness(const cv::Mat& frame) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);

    cv::Scalar mean, stddev;
    cv::meanStdDev(laplacian, mean, stddev);
    return stddev[0] * stddev[0];  // variance = sharpness score
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

    std::ofstream meta(dir / "metadata.json");
    meta << "{\n"
         << "  \"timestamp\": \"" << timestamp << "\",\n"
         << "  \"change_score\": " << event.changeScore << ",\n"
         << "  \"sharpness_score\": " << sharpness << "\n"
         << "}\n";

    ++nextId_;
}

}  // namespace palim
