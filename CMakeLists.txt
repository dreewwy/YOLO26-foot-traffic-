cmake_minimum_required(VERSION 3.10)
project(people_counter)

set(CMAKE_CXX_STANDARD 17)

# Find OpenCV and OpenVINO
find_package(OpenCV REQUIRED)
find_package(OpenVINO REQUIRED)

add_executable(people_counter main.cpp tracker.cpp)

# Link the libraries
target_link_libraries(people_counter PRIVATE ${OpenCV_LIBS} openvino::runtime)
