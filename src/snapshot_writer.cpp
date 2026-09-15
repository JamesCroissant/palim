#include "snapshot_writer.hpp"

#include <iomanip>
#include <sstream>

#include <opencv2/imgcodecs.hpp>

namespace palim {

SnapshotWriter::SnapshotWriter(std::filesystem::path outputDir) : outputDir_(std::move(outputDir)) {
    std::filesystem::create_directories(outputDir_);
}

void SnapshotWriter::write(const CommitEvent& event) {
    std::ostringstream id;
    id << std::setw(3) << std::setfill('0') << nextId_;

    const std::filesystem::path dir = outputDir_ / id.str();
    std::filesystem::create_directories(dir);

    cv::imwrite((dir / "before.jpg").string(), event.before);
    cv::imwrite((dir / "after.jpg").string(), event.after);

    ++nextId_;
}

}  // namespace palim
