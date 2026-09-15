#include <chrono>
#include <iostream>
#include <optional>

#include <opencv2/highgui.hpp>

#include "camera.hpp"
#include "change_detector.hpp"
#include "snapshot_writer.hpp"
#include "state_machine.hpp"

// Step 4: StateMachine now owns the CHANGING / WAIT_FOR_STABLE decision;
// main.cpp just reports whatever CommitEvent (if any) comes back.
namespace {
constexpr double kChangeThresholdPercent = 5.0;
constexpr auto kStableDuration = std::chrono::seconds(2);
}

int main() {
    palim::Camera camera(0);
    if (!camera.isOpened()) {
        std::cerr << "Failed to open camera at index 0\n";
        return 1;
    }

    palim::ChangeDetector detector;
    palim::StateMachine stateMachine(kChangeThresholdPercent, kStableDuration);
    palim::SnapshotWriter snapshotWriter;

    const std::string windowName = "palim";
    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);

    std::optional<cv::Mat> prevFrame;

    while (true) {
        auto frame = camera.grab();
        if (!frame) {
            std::cerr << "Frame grab failed, retrying...\n";
            continue;
        }

        if (prevFrame) {
            const double changedPercent = detector.compare(*prevFrame, *frame);
            std::cout << "changed: " << changedPercent << "%\n";
            if (changedPercent > kChangeThresholdPercent) {
                std::cout << "CHANGE DETECTED\n";
            }

            auto commit = stateMachine.update(changedPercent, *frame, std::chrono::steady_clock::now());
            if (commit) {
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
