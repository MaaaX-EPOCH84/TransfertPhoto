cmake_minimum_required(VERSION 3.28)

# À poser avant project() : les deux initialisent la chaîne de compilation macOS.
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures cibles sous macOS")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Version minimale de macOS")

project(TransfertPhoto VERSION 0.1.0 LANGUAGES CXX)

option(TRANSFERTPHOTO_BUILD_TESTS "Construire les tests unitaires" ON)

# Runtime C statique : le livrable tient en un seul fichier et ne doit pas
# dépendre du redistribuable Visual C++. À poser avant la déclaration de
# wxWidgets, qui lit cette variable pour se construire sur le même runtime.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Avertissements pour nos seules cibles ; wxWidgets est consommé en SYSTEM.
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
