#include "camera.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <utility>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <opencv2/imgproc.hpp>

namespace {

// ioctl() can return EINTR if interrupted by a signal mid-call; retrying
// in that case (rather than treating it as a real failure) is the
// standard pattern the V4L2 documentation itself recommends.
int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

}  // namespace

namespace palim {

Camera::Camera(const std::string& devicePath, int width, int height) : width_(width), height_(height) {
    if (!openDevice(devicePath)) return;
    if (!queryCapabilities() || !setFormat(width, height) || !requestBuffers() || !mapBuffers() ||
        !queueAllBuffers() || !startStreaming()) {
        closeDevice();
    }
}

Camera::~Camera() {
    closeDevice();
}

Camera::Camera(Camera&& other) noexcept {
    *this = std::move(other);
}

Camera& Camera::operator=(Camera&& other) noexcept {
    if (this != &other) {
        closeDevice();
        fd_ = other.fd_;
        width_ = other.width_;
        height_ = other.height_;
        buffers_ = std::move(other.buffers_);
        streaming_ = other.streaming_;

        other.fd_ = -1;
        other.streaming_ = false;
        other.buffers_.clear();
    }
    return *this;
}

bool Camera::openDevice(const std::string& devicePath) {
    fd_ = open(devicePath.c_str(), O_RDWR);
    if (fd_ < 0) {
        std::cerr << "Camera: open(" << devicePath << ") failed: " << std::strerror(errno) << "\n";
        return false;
    }
    return true;
}

bool Camera::queryCapabilities() {
    v4l2_capability cap{};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        std::cerr << "Camera: VIDIOC_QUERYCAP failed: " << std::strerror(errno) << "\n";
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        std::cerr << "Camera: device does not support video capture\n";
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) {
        std::cerr << "Camera: device does not support streaming I/O\n";
        return false;
    }
    return true;
}

bool Camera::setFormat(int width, int height) {
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = static_cast<__u32>(width);
    fmt.fmt.pix.height = static_cast<__u32>(height);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        std::cerr << "Camera: VIDIOC_S_FMT failed: " << std::strerror(errno) << "\n";
        return false;
    }

    // The driver is allowed to grant something other than what we asked
    // for (e.g. a resolution the sensor doesn't actually support) —
    // VIDIOC_S_FMT adjusts fmt in place to what it actually set, so we
    // must read that back rather than assume our request was honored.
    width_ = static_cast<int>(fmt.fmt.pix.width);
    height_ = static_cast<int>(fmt.fmt.pix.height);
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) {
        std::cerr << "Camera: driver did not grant YUYV format\n";
        return false;
    }
    return true;
}

bool Camera::requestBuffers() {
    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "Camera: VIDIOC_REQBUFS failed: " << std::strerror(errno) << "\n";
        return false;
    }
    if (req.count < 2) {
        std::cerr << "Camera: driver granted too few buffers (" << req.count << ")\n";
        return false;
    }
    buffers_.resize(req.count);
    return true;
}

bool Camera::mapBuffers() {
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = static_cast<__u32>(i);

        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "Camera: VIDIOC_QUERYBUF failed: " << std::strerror(errno) << "\n";
            return false;
        }

        void* start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
        if (start == MAP_FAILED) {
            std::cerr << "Camera: mmap failed: " << std::strerror(errno) << "\n";
            return false;
        }
        buffers_[i] = MappedBuffer{start, buf.length};
    }
    return true;
}

bool Camera::queueAllBuffers() {
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = static_cast<__u32>(i);

        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "Camera: VIDIOC_QBUF failed: " << std::strerror(errno) << "\n";
            return false;
        }
    }
    return true;
}

bool Camera::startStreaming() {
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "Camera: VIDIOC_STREAMON failed: " << std::strerror(errno) << "\n";
        return false;
    }
    streaming_ = true;
    return true;
}

void Camera::stopStreaming() {
    if (!streaming_) return;
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);
    streaming_ = false;
}

void Camera::closeDevice() {
    stopStreaming();
    for (auto& buf : buffers_) {
        if (buf.start != nullptr && buf.start != MAP_FAILED) {
            munmap(buf.start, buf.length);
        }
    }
    buffers_.clear();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
}

std::optional<cv::Mat> Camera::grab() {
    if (!isOpened()) {
        return std::nullopt;
    }

    v4l2_buffer buf{};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Blocks until the driver has a filled buffer ready.
    if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        std::cerr << "Camera: VIDIOC_DQBUF failed: " << std::strerror(errno) << "\n";
        return std::nullopt;
    }

    // Wrap the driver's buffer without copying, then immediately convert
    // (which does copy, into a Mat we allocate and own) — we must not
    // touch this memory again after VIDIOC_QBUF hands it back below.
    const cv::Mat yuyv(height_, width_, CV_8UC2, buffers_[buf.index].start);
    cv::Mat bgr;
    cv::cvtColor(yuyv, bgr, cv::COLOR_YUV2BGR_YUYV);

    if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        std::cerr << "Camera: VIDIOC_QBUF (requeue) failed: " << std::strerror(errno) << "\n";
    }

    return bgr;
}

std::optional<int> Camera::getControl(int controlId) const {
    if (fd_ < 0) {
        return std::nullopt;
    }
    v4l2_control ctrl{};
    ctrl.id = static_cast<__u32>(controlId);
    if (xioctl(fd_, VIDIOC_G_CTRL, &ctrl) < 0) {
        return std::nullopt;  // control not supported by this device
    }
    return ctrl.value;
}

CameraSettings Camera::currentSettings() const {
    CameraSettings settings;
    settings.width = width_;
    settings.height = height_;
    settings.exposureAuto = getControl(V4L2_CID_EXPOSURE_AUTO);
    settings.exposureAbsolute = getControl(V4L2_CID_EXPOSURE_ABSOLUTE);
    settings.gain = getControl(V4L2_CID_GAIN);
    settings.whiteBalanceAuto = getControl(V4L2_CID_AUTO_WHITE_BALANCE);
    settings.whiteBalanceTemperature = getControl(V4L2_CID_WHITE_BALANCE_TEMPERATURE);
    return settings;
}

}  // namespace palim
