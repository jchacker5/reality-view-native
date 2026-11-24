cmake_minimum_required(VERSION 3.24)

project(RealityViewNative
    VERSION 0.1.0
    DESCRIPTION "Native C++20 / Qt 6 real-time vehicle visualization with simulated telemetry"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RVN_BUILD_APP "Build the Qt Quick application (needs Qt Quick)" ON)
option(RVN_BUILD_TESTS "Build unit tests" ON)
option(RVN_BUILD_BENCH "Build benchmarks and tools" ON)
option(RVN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(RVN_SANITIZE "" CACHE STRING "Sanitizers to enable: '', 'address,undefined', or 'thread'")

include(cmake/ProjectOptions.cmake)

# The core library is Qt-free. Qt is only needed for the app and for the Qt Test runner.
if(RVN_BUILD_APP)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2)
    qt_standard_project_setup(REQUIRES 6.5)
endif()
if(RVN_BUILD_TESTS)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Core Test)
    enable_testing()
endif()

find_package(Threads REQUIRED)

add_subdirectory(src/core)
if(RVN_BUILD_APP)
    add_subdirectory(src/app)
endif()
if(RVN_BUILD_TESTS)
    add_subdirectory(tests)
endif()
if(RVN_BUILD_BENCH)
    add_subdirectory(bench)
    add_subdirectory(tools)
endif()
