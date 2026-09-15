#include <iostream>
#include <optional>

#include <opencv2/highgui.hpp>

#include "camera.hpp"
#include "change_detector.hpp"

// Step 2: adds per-frame change percentage, printed to the terminal.
// Still no decision-making about what the number means — that's Step 3.
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
