cmake_minimum_required(VERSION 3.16)
project(palim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Unlike this account's other two from-scratch projects (os-practice's
# Makefile has carried -Wall -Wextra since its very first commit;
# focus-monitor's CMakeLists.txt gained the same, plus -Wshadow/
# -Wpedantic/-Werror, across several days once the gap was noticed),
# this project had no compiler warnings enabled at all. Applied
# globally, before any add_library/add_executable below, so palim_lib,
# both real executables, and the test binary all get it. -Werror is
# included from the start here (not added separately later, the way
# focus-monitor's own history did it) since every warning this actually
# surfaced is fixed in the same change that adds the flags -- see
# frame_history.hpp's own comment on BufferedFrame, and the three
# range-for loops in tests/test_commit_metadata.cpp.
add_compile_options(-Wall -Wextra -Wshadow -Wpedantic -Werror)

# No more videoio: Camera now talks to V4L2 directly (open/ioctl/mmap)
# instead of going through cv::VideoCapture.
find_package(OpenCV REQUIRED COMPONENTS core imgproc highgui imgcodecs)
find_package(Threads REQUIRED)

# All the logic, shared by both executables and by the test suite. Kept
# as a static library (not just object files piled into each binary)
# so it's built once; static linking still only pulls in the .o's an
# executable actually references, so palim-timeline picking this up
# doesn't drag OpenCV into it.
add_library(palim_lib STATIC
    src/camera.cpp
    src/change_detector.cpp
    src/state_machine.cpp
    src/snapshot_writer.cpp
    src/frame_quality.cpp
    src/frame_history.cpp
    src/git_info.cpp
    src/commit_metadata.cpp
    src/config.cpp
    src/timeline_format.cpp
)
target_include_directories(palim_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# SYSTEM, separate from this project's own src/ above: -Werror means ANY
# warning now fails the build, including one that originates inside a
# header this project doesn't own. CI's own build step installs
# whatever libopencv-dev the runner's current package index has, not a
# version pinned anywhere in this repo -- a future OpenCV upgrade adding
# so much as one new deprecation warning to its own public headers
# would otherwise break this build for a reason nothing in this repo
# caused or can fix by itself. SYSTEM suppresses warnings that
# originate inside OpenCV's own headers regardless of -Wall/-Wextra/
# -Wshadow/-Wpedantic/-Werror, the same treatment the C++ standard
# library's own headers already get, while leaving all of those fully
# enforced against this project's own code.
target_include_directories(palim_lib SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
target_link_libraries(palim_lib PUBLIC ${OpenCV_LIBS} Threads::Threads)

add_executable(palim src/main.cpp)
target_link_libraries(palim PRIVATE palim_lib)

add_executable(palim-timeline src/timeline_main.cpp)
target_link_libraries(palim-timeline PRIVATE palim_lib)

option(PALIM_BUILD_TESTS "Build the test suite (requires GTest)" ON)
if(PALIM_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)

    add_executable(palim_tests
        tests/test_change_detector.cpp
        tests/test_state_machine.cpp
        tests/test_ring_buffer.cpp
        tests/test_frame_history.cpp
        tests/test_frame_quality.cpp
        tests/test_blocking_queue.cpp
        tests/test_git_info.cpp
        tests/test_commit_metadata.cpp
        tests/test_camera.cpp
        tests/test_config.cpp
        tests/test_shutdown_flag.cpp
        tests/test_snapshot_writer.cpp
        tests/test_consecutive_failure_guard.cpp
        tests/test_display_availability.cpp
        tests/test_timeline_format.cpp
    )
    target_link_libraries(palim_tests PRIVATE palim_lib GTest::gtest GTest::gtest_main)
    # So test_git_info.cpp can cd into a directory known to be a git repo
    # (this checkout) regardless of where the test binary itself runs from.
    target_compile_definitions(palim_tests PRIVATE PALIM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    include(GoogleTest)
    gtest_discover_tests(palim_tests)
endif()
