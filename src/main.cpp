#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

#include <opencv2/highgui.hpp>

#include "blocking_queue.hpp"
#include "camera.hpp"
#include "change_detector.hpp"
#include "frame_history.hpp"
#include "snapshot_writer.hpp"
#include "state_machine.hpp"

// Phase 3: capture / processing / storage now run on separate threads,
// connected by BlockingQueue<T>. ChangeDetector, StateMachine, and
// FrameHistory move inside the processing thread's own function (rather
// than being constructed in main and shared) so nothing touches them
// from more than one thread -- no locking needed for any of them.
namespace {
constexpr double kChangeThresholdPercent = 5.0;
constexpr auto kStableDuration = std::chrono::seconds(2);
// ~30fps * 3s of headroom, per the design doc's own sizing example.
constexpr std::size_t kFrameHistoryCapacity = 90;
// Small on purpose: only the most recent frames are worth processing: if
// processing falls behind, dropping a stale frame is fine.
constexpr std::size_t kFrameQueueCapacity = 4;
// Generous on purpose: commits are rare (at most one per ~2s+ of
// stability) and each one matters, so this should never realistically
// fill up and drop anything.
constexpr std::size_t kCommitQueueCapacity = 64;
}  // namespace

int main() {
    palim::Camera camera("/dev/video0");
    if (!camera.isOpened()) {
        std::cerr << "Failed to open camera at /dev/video0\n";
        return 1;
    }

    std::atomic<bool> running{true};
    palim::BlockingQueue<palim::FrameSample> frameQueue(kFrameQueueCapacity);
    palim::BlockingQueue<palim::CommitEvent> commitQueue(kCommitQueueCapacity);

    // Shared only with the main thread, only for the preview window.
    // The mutex isn't protecting the pixel buffer (cv::Mat's refcounting
    // already makes sharing that safely between threads fine, since
    // nothing here ever mutates a frame's pixels in place) -- it's
    // protecting the cv::Mat *header* itself: assigning a new value into
    // displayFrame touches several of its fields, non-atomically, and a
    // concurrent reader could otherwise see a half-updated header.
    std::mutex displayMutex;
    cv::Mat displayFrame;

    // --- Capture thread: the only thread that touches `camera`.
    // "camera = std::move(camera)" move-captures the outer `camera` into
    // a same-named variable local to the lambda; `mutable` is needed
    // because grab() is non-const and lambda captures are const by
    // default.
    std::thread captureThread([&, camera = std::move(camera)]() mutable {
        while (running.load()) {
            auto frame = camera.grab();
            if (!frame) {
                continue;
            }
            const auto timestamp = std::chrono::steady_clock::now();

            {
                std::lock_guard<std::mutex> lock(displayMutex);
                displayFrame = *frame;
            }
            frameQueue.push(palim::FrameSample{timestamp, std::move(*frame)});
        }
        // No more frames coming: let the processing thread know so it
        // can drain what's left and exit instead of blocking forever.
        frameQueue.shutdown();
    });

    // --- Processing thread: everything stateful about "has the desk
    // changed" lives here, and only here.
    std::thread processingThread([&] {
        palim::ChangeDetector detector;
        palim::StateMachine stateMachine(kChangeThresholdPercent, kStableDuration);
        palim::FrameHistory frameHistory(kFrameHistoryCapacity);
        std::optional<cv::Mat> prevFrame;

        while (true) {
            auto sample = frameQueue.waitAndPop();
            if (!sample) {
                break;  // capture thread shut the queue down
            }

            frameHistory.push(sample->frame, sample->timestamp);

            if (prevFrame) {
                const double changedPercent = detector.compare(*prevFrame, sample->frame);
                std::cout << "changed: " << changedPercent << "%\n";
                if (changedPercent > kChangeThresholdPercent) {
                    std::cout << "CHANGE DETECTED\n";
                }

                // Use the frame's own capture timestamp, not "now" -- the
                // queue between capture and here can add latency, and
                // stability should be measured against when things
                // actually happened on the desk.
                auto commit = stateMachine.update(changedPercent, sample->frame, sample->timestamp);
                if (commit) {
                    if (auto best =
                            frameHistory.bestFrameInRange(commit->stableWindowStart, commit->stableWindowEnd)) {
                        commit->after = *best;
                    }
                    commitQueue.push(std::move(*commit));
                }
            }
            prevFrame = sample->frame;
        }
        commitQueue.shutdown();
    });

    // --- Storage thread: the only thread that touches disk.
    std::thread storageThread([&] {
        palim::SnapshotWriter snapshotWriter;
        while (true) {
            auto commit = commitQueue.waitAndPop();
            if (!commit) {
                break;  // processing thread shut the queue down
            }
            snapshotWriter.write(*commit);
            std::cout << "COMMIT saved\n";
        }
    });

    // --- Main thread: owns the GUI. OpenCV's highgui isn't guaranteed
    // thread-safe across backends, so window creation / imshow / waitKey
    // all stay here rather than moving into captureThread.
    const std::string windowName = "palim";
    cv::namedWindow(windowName, cv::WINDOW_AUTOSIZE);

    while (running.load()) {
        cv::Mat frame;
        {
            std::lock_guard<std::mutex> lock(displayMutex);
            frame = displayFrame;
        }
        if (!frame.empty()) {
            cv::imshow(windowName, frame);
        }

        // waitKey also pumps the GUI event loop; without it no window
        // draws. 30ms caps the display refresh rate instead of spinning
        // as fast as possible.
        if (cv::waitKey(30) == 27) {  // Esc
            running.store(false);
        }
    }

    captureThread.join();
    processingThread.join();
    storageThread.join();
    return 0;
}
