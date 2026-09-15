#include <iostream>
#include <optional>

#include <opencv2/highgui.hpp>

#include "camera.hpp"
#include "change_detector.hpp"

// Step 3: adds a fixed threshold on top of Step 2's percentage. Deciding
// "significant enough to count as change" belongs here for now; deciding
// what to *do* about it (wait for stability, commit once) is Step 4's
// StateMachine, not this.
namespace {
constexpr double kChangeThresholdPercent = 5.0;
}

int main() {
    palim::Camera camera(0);
    if (!camera.isOpened()) {
        std::cerr << "Failed to open camera at index 0\n";
        return 1;
    }

    palim::ChangeDetector detector;

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
