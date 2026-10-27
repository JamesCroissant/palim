#include <chrono>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/highgui.hpp>

#include "blocking_queue.hpp"
#include "camera.hpp"
#include "change_detector.hpp"
#include "config.hpp"
#include "consecutive_failure_guard.hpp"
#include "frame_history.hpp"
#include "shutdown_flag.hpp"
#include "snapshot_writer.hpp"
#include "state_machine.hpp"

// Phase 3: capture / processing / storage now run on separate threads,
// connected by BlockingQueue<T>. ChangeDetector, StateMachine, and
// FrameHistory move inside the processing thread's own function (rather
// than being constructed in main and shared) so nothing touches them
// from more than one thread -- no locking needed for any of them.
namespace {
// Small on purpose: only the most recent frames are worth processing: if
// processing falls behind, dropping a stale frame is fine.
constexpr std::size_t kFrameQueueCapacity = 4;
// Generous on purpose: commits are rare (at most one per ~2s+ of
// stability) and each one matters, so this should never realistically
// fill up and drop anything.
constexpr std::size_t kCommitQueueCapacity = 64;
// Camera::grab() fails fast (no blocking ioctl succeeds) rather than
// taking the normal blocking-until-a-real-frame path, so a disconnected
// camera or a stuck driver would otherwise spin the capture thread at
// 100% CPU forever with nothing to notice or stop it. ~300ms of
// consecutive failures (at the 10ms retry delay below) is long enough
// to absorb a brief multi-frame hiccup without mistaking it for a dead
// camera.
constexpr int kMaxConsecutiveGrabFailures = 30;
constexpr auto kGrabFailureRetryDelay = std::chrono::milliseconds(10);
}  // namespace

int main(int argc, char** argv) {
    palim::Config config;
    try {
        config = palim::parseArgs(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const std::invalid_argument& e) {
        std::cerr << "Argument error: " << e.what() << "\n";
        std::cerr << "Usage: palim [--device PATH] [--width PX] [--height PX] "
                     "[--threshold PERCENT] [--stable-ms MS] [--output-dir DIR] "
                     "[--frame-history-capacity FRAMES]\n";
        return 1;
    }

    // Before anything else starts: Ctrl-C/SIGTERM used to just kill the
    // process, abandoning whichever thread was mid-flight -- most
    // importantly the storage thread, possibly mid-SnapshotWriter::write(),
    // which would leave a half-written commits/NNN/ directory behind.
    // ShutdownFlag::requested() now drives every loop below the same way
    // Esc already did, so a signal joins all three threads cleanly
    // instead of corrupting whatever commit was in progress.
    palim::installShutdownSignalHandlers();

    palim::Camera camera(config.devicePath, config.width, config.height);
    if (!camera.isOpened()) {
        std::cerr << "Failed to open camera at " << config.devicePath << "\n";
        return 1;
    }

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
        palim::ConsecutiveFailureGuard grabFailureGuard(kMaxConsecutiveGrabFailures);
        while (!palim::ShutdownFlag::instance().requested()) {
            auto frame = camera.grab();
            if (!frame) {
                if (grabFailureGuard.recordFailure()) {
                    std::cerr << "Camera: " << kMaxConsecutiveGrabFailures
                              << " consecutive failed grabs -- giving up\n";
                    palim::ShutdownFlag::instance().request();
                    break;
                }
                std::this_thread::sleep_for(kGrabFailureRetryDelay);
                continue;
            }
            grabFailureGuard.recordSuccess();
            const auto timestamp = std::chrono::steady_clock::now();
            const auto settings = camera.currentSettings();

            {
                std::lock_guard<std::mutex> lock(displayMutex);
                displayFrame = *frame;
            }
            frameQueue.push(palim::FrameSample{timestamp, std::move(*frame), settings});
        }
        // No more frames coming: let the processing thread know so it
        // can drain what's left and exit instead of blocking forever.
        frameQueue.shutdown();
    });

    // --- Processing thread: everything stateful about "has the desk
    // changed" lives here, and only here.
    std::thread processingThread([&] {
        palim::ChangeDetector detector;
        palim::StateMachine stateMachine(config.changeThresholdPercent, config.stableDuration);
        palim::FrameHistory frameHistory(static_cast<std::size_t>(config.frameHistoryCapacity));
        std::optional<cv::Mat> prevFrame;

        while (true) {
            auto sample = frameQueue.waitAndPop();
            if (!sample) {
                break;  // capture thread shut the queue down
            }

            frameHistory.push(sample->frame, sample->timestamp);

            if (prevFrame) {
                const auto changeResult = detector.compareWithRegion(*prevFrame, sample->frame);
                std::cout << "changed: " << changeResult.changedPercent << "%\n";
                if (changeResult.changedPercent > config.changeThresholdPercent) {
                    std::cout << "CHANGE DETECTED\n";
                }

                // Use the frame's own capture timestamp, not "now" -- the
                // queue between capture and here can add latency, and
                // stability should be measured against when things
                // actually happened on the desk.
                auto commit = stateMachine.update(changeResult.changedPercent, changeResult.changedRegion,
                                                   sample->frame, sample->timestamp, sample->settings);
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
        palim::SnapshotWriter snapshotWriter(config.outputDir);
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

    while (!palim::ShutdownFlag::instance().requested()) {
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
            palim::ShutdownFlag::instance().request();
        }
    }

    captureThread.join();
    processingThread.join();
    storageThread.join();
    return 0;
}
