cmake_minimum_required(VERSION 3.16)
project(people_counter)

set(CMAKE_CXX_STANDARD 17)

# OpenCV
set(OpenCV_DIR "C:/opencv/build")
find_package(OpenCV REQUIRED)

# OpenVINO
find_package(OpenVINO REQUIRED COMPONENTS Runtime)

add_executable(people_counter main.cpp tracker.cpp)

target_link_libraries(people_counter
    ${OpenCV_LIBS}
    openvino::runtime
)