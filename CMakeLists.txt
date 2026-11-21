cmake_minimum_required(VERSION 3.28)

# Both must be set before project(): they initialise the macOS toolchain.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS target architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(TransfertPhoto VERSION 0.1.0 LANGUAGES CXX)

option(TRANSFERTPHOTO_BUILD_TESTS "Build the unit tests" ON)

# Static CRT: the deliverable is a single file and must not depend on the
# Visual C++ redistributable. Set before wxWidgets is declared, which reads it
# to build itself against the same runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Warnings for our own targets only; wxWidgets is consumed as SYSTEM.
add_library(transfertphoto_warnings INTERFACE)
target_compile_options(transfertphoto_warnings INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>)

add_subdirectory(src/core)

include(cmake/wxwidgets.cmake)
add_subdirectory(src/app)

if(TRANSFERTPHOTO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
