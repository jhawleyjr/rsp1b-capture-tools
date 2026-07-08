cmake_minimum_required(VERSION 3.16)

project(sdrplay_rsp1b_tools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(rsp1b_probe src/rsp1b_probe.cpp)
add_executable(rsp1b_capture src/rsp1b_capture.cpp)

target_include_directories(rsp1b_probe PRIVATE /usr/local/include)
target_link_libraries(rsp1b_probe PRIVATE /usr/local/lib/libsdrplay_api.dylib)
target_link_options(rsp1b_probe PRIVATE "-Wl,-rpath,/usr/local/lib")

target_include_directories(rsp1b_capture PRIVATE /usr/local/include)
target_link_libraries(rsp1b_capture PRIVATE /usr/local/lib/libsdrplay_api.dylib)
target_link_options(rsp1b_capture PRIVATE "-Wl,-rpath,/usr/local/lib")
