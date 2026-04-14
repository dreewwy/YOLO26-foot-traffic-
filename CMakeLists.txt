cmake_minimum_required(VERSION 3.10)
project(people_counter)

# Use C++ 17 standard
set(CMAKE_CXX_STANDARD 17)

# Find OpenCV and OpenVINO
find_package(OpenCV REQUIRED)
find_package(OpenVINO REQUIRED)

# Compile the executable (make sure to include your tracker source file!)
add_executable(people_counter main.cpp tracker.cpp)

# Link the libraries
target_link_libraries(people_counter PRIVATE ${OpenCV_LIBS} openvino::runtime)
