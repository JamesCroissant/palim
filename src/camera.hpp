#pragma once

#include <optional>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace palim {

// Talks to a V4L2 capture device directly: open() + ioctl() + mmap(),
// no cv::VideoCapture. The public interface (grab() -> optional<Mat>) is
// unchanged from the Phase 1 version, so nothing downstream in the
// pipeline needed to change for this rewrite.
//
// Capture cycle, once streaming:
//   VIDIOC_DQBUF  -- driver hands us a filled buffer (blocks until ready)
//   convert YUYV -> BGR into a cv::Mat we own
//   VIDIOC_QBUF   -- hand the (now-empty) buffer back to the driver
//
// The mmap'd buffers belong to the kernel driver, not to us: the instant
// we VIDIOC_QBUF a buffer back, the driver may overwrite it with the next
// frame. So grab() must convert (which copies) before requeuing — the
// same "don't keep a bare reference past its owner's next write" rule as
// FrameHistory's clone(), just one layer lower, at the kernel buffer
// level instead of the cv::Mat level.
class Camera {
public:
    explicit Camera(const std::string& devicePath, int width = 1280, int height = 720);
    ~Camera();

    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    Camera(Camera&& other) noexcept;
    Camera& operator=(Camera&& other) noexcept;

    bool isOpened() const { return fd_ >= 0 && streaming_; }

    std::optional<cv::Mat> grab();

private:
    struct MappedBuffer {
        void* start = nullptr;
        std::size_t length = 0;
    };

    bool openDevice(const std::string& devicePath);
    bool queryCapabilities();
    bool setFormat(int width, int height);
    bool requestBuffers();
    bool mapBuffers();
    bool queueAllBuffers();
    bool startStreaming();
    void stopStreaming();
    void closeDevice();

    int fd_ = -1;
    int width_ = 0;
    int height_ = 0;
    std::vector<MappedBuffer> buffers_;
    bool streaming_ = false;
};

}  // namespace palim
