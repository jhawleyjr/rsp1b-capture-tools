cmake_minimum_required(VERSION 3.16)

project(rsp1b_capture_tools VERSION 0.1.0 LANGUAGES CXX)

option(RSP1B_BUILD_TOOLS "Build the SDRplay RSP1B hardware tools" ON)
set(SDRPLAY_API_ROOT "" CACHE PATH "Root directory of the SDRplay API installation")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Threads REQUIRED)

function(rsp1b_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(rsp1b_core
    src/capture_options.cpp
    src/iq_writer.cpp
    src/metadata.cpp
    src/timestamp.cpp
)
target_include_directories(rsp1b_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rsp1b_core PUBLIC Threads::Threads)
rsp1b_enable_warnings(rsp1b_core)

if(RSP1B_BUILD_TOOLS)
    set(_sdrplay_hints)
    if(SDRPLAY_API_ROOT)
        list(APPEND _sdrplay_hints "${SDRPLAY_API_ROOT}")
    endif()
    if(DEFINED ENV{SDRPLAY_API_ROOT} AND NOT "$ENV{SDRPLAY_API_ROOT}" STREQUAL "")
        list(APPEND _sdrplay_hints "$ENV{SDRPLAY_API_ROOT}")
    endif()

    find_path(SDRPLAY_API_INCLUDE_DIR
        NAMES sdrplay_api.h
        HINTS ${_sdrplay_hints}
        PATH_SUFFIXES include Include
    )
    find_library(SDRPLAY_API_LIBRARY
        NAMES sdrplay_api
        HINTS ${_sdrplay_hints}
        PATH_SUFFIXES lib lib64 Lib
    )

    if(NOT SDRPLAY_API_INCLUDE_DIR OR NOT SDRPLAY_API_LIBRARY)
        message(FATAL_ERROR
            "The SDRplay API SDK was not found. Set SDRPLAY_API_ROOT to the SDK installation "
            "prefix, or configure with -DRSP1B_BUILD_TOOLS=OFF for portable tests only.")
    endif()

    add_library(SDRplay::API UNKNOWN IMPORTED)
    set_target_properties(SDRplay::API PROPERTIES
        IMPORTED_LOCATION "${SDRPLAY_API_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SDRPLAY_API_INCLUDE_DIR}"
    )

    add_library(rsp1b_device
        src/rsp1b_device.cpp
        src/signal_stop.cpp
    )
    target_include_directories(rsp1b_device PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(rsp1b_device PUBLIC SDRplay::API Threads::Threads)
    rsp1b_enable_warnings(rsp1b_device)

    add_executable(rsp1b_probe src/rsp1b_probe.cpp)
    target_link_libraries(rsp1b_probe PRIVATE rsp1b_device)
    rsp1b_enable_warnings(rsp1b_probe)

    add_executable(rsp1b_capture src/rsp1b_capture.cpp)
    target_link_libraries(rsp1b_capture PRIVATE rsp1b_core rsp1b_device)
    rsp1b_enable_warnings(rsp1b_capture)
endif()

if(BUILD_TESTING)
    add_executable(capture_options_test tests/capture_options_test.cpp)
    target_link_libraries(capture_options_test PRIVATE rsp1b_core)
    rsp1b_enable_warnings(capture_options_test)
    add_test(NAME capture_options COMMAND capture_options_test)

    add_executable(metadata_test tests/metadata_test.cpp)
    target_link_libraries(metadata_test PRIVATE rsp1b_core)
    rsp1b_enable_warnings(metadata_test)
    add_test(NAME metadata COMMAND metadata_test)

    add_executable(iq_writer_test tests/iq_writer_test.cpp)
    target_link_libraries(iq_writer_test PRIVATE rsp1b_core)
    rsp1b_enable_warnings(iq_writer_test)
    add_test(NAME iq_writer COMMAND iq_writer_test)
endif()
