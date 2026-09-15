#include <iostream>

#include <opencv2/highgui.hpp>

#include "camera.hpp"

// Step 1: open the camera and show what it sees. Nothing else yet.
int main() {
    palim::Camera camera(0);
    if (!camera.isOpened()) {
        std::cerr << "Failed to open camera at index 0\n";
        return 1;
    }

    const std::string windowName = "palim";
    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);

    while (true) {
        auto frame = camera.grab();
        if (!frame) {
            std::cerr << "Frame grab failed, retrying...\n";
            continue;
        }

        cv::imshow(windowName, *frame);

        // waitKey also pumps the GUI event loop; without it no window draws.
        if (cv::waitKey(1) == 27) {  // Esc
            break;
        }
    }

    return 0;
}
