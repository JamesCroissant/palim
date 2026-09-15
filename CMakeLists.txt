cmake_minimum_required(VERSION 3.16)
project(palim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS core imgproc videoio highgui)

add_executable(palim
    src/main.cpp
    src/camera.cpp
    src/change_detector.cpp
)

target_include_directories(palim PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(palim PRIVATE ${OpenCV_LIBS})
