cmake_minimum_required(VERSION 3.16)
project(palim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# No more videoio: Camera now talks to V4L2 directly (open/ioctl/mmap)
# instead of going through cv::VideoCapture.
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui imgcodecs)
find_package(Threads REQUIRED)

add_executable(palim
    src/main.cpp
    src/camera.cpp
    src/change_detector.cpp
    src/state_machine.cpp
    src/snapshot_writer.cpp
    src/frame_quality.cpp
    src/frame_history.cpp
)

target_include_directories(palim PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(palim PRIVATE ${OpenCV_LIBS} Threads::Threads)
