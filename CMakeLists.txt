cmake_minimum_required(VERSION 3.20)
project(pacer LANGUAGES C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "Pacer requires MSVC (Windows-only project)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

add_compile_definitions(
    NOMINMAX WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00 WINVER=0x0A00
    UNICODE _UNICODE
)
add_compile_options(/utf-8 /W3 /permissive-)

# Static CRT so the injected DLL carries no redist dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# All binaries land in build/bin/<Config>/ so the injector finds PacerCore.dll
# next to itself by default.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/$<CONFIG>)

add_subdirectory(external/minhook)

add_library(pacer_shared INTERFACE)
target_include_directories(pacer_shared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(core)
add_subdirectory(service)
add_subdirectory(ui)
add_subdirectory(tools/injector)
add_subdirectory(tools/statsreader)
add_subdirectory(tools/eject)
add_subdirectory(testharness/spinner)
add_subdirectory(testharness/d9spinner)
add_subdirectory(testharness/glspinner)
add_subdirectory(testharness/vkspinner)
add_subdirectory(testharness/idle)

