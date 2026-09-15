#include <chrono>
#include <iostream>
#include <optional>

#include <opencv2/highgui.hpp>

#include "camera.hpp"
#include "change_detector.hpp"
#include "frame_history.hpp"
#include "snapshot_writer.hpp"
#include "state_machine.hpp"

// Phase 1.5: FrameHistory keeps a rolling window of recent frames so a
// commit can pick the sharpest one from the stable period, rather than
// whatever frame happened to be current the instant StateMachine fired.
namespace {
constexpr double kChangeThresholdPercent = 5.0;
constexpr auto kStableDuration = std::chrono::seconds(2);
// ~30fps * 3s of headroom, per the design doc's own sizing example.
constexpr std::size_t kFrameHistoryCapacity = 90;
}

int main() {
    palim::Camera camera("/dev/video0");
    if (!camera.isOpened()) {
        std::cerr << "Failed to open camera at /dev/video0\n";
        return 1;
    }

    palim::ChangeDetector detector;
    palim::StateMachine stateMachine(kChangeThresholdPercent, kStableDuration);
    palim::SnapshotWriter snapshotWriter;
    palim::FrameHistory frameHistory(kFrameHistoryCapacity);

    const std::string windowName = "palim";
    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);

    std::optional<cv::Mat> prevFrame;

    while (true) {
        auto frame = camera.grab();
        if (!frame) {
            std::cerr << "Frame grab failed, retrying...\n";
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        frameHistory.push(*frame, now);

        if (prevFrame) {
            const double changedPercent = detector.compare(*prevFrame, *frame);
            std::cout << "changed: " << changedPercent << "%\n";
            if (changedPercent > kChangeThresholdPercent) {
                std::cout << "CHANGE DETECTED\n";
            }

            auto commit = stateMachine.update(changedPercent, *frame, now);
            if (commit) {
                if (auto best = frameHistory.bestFrameInRange(commit->stableWindowStart, commit->stableWindowEnd)) {
                    commit->after = *best;
                }
                snapshotWriter.write(*commit);
                std::cout << "COMMIT saved\n";
            }
        }
        prevFrame = frame;

        cv::imshow(windowName, *frame);

        // waitKey also pumps the GUI event loop; without it no window draws.
        if (cv::waitKey(1) == 27) {  // Esc
            break;
        }
    }

    return 0;
}
