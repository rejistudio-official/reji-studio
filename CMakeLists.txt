cmake_minimum_required(VERSION 3.20)
project(reji_studio VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform tespiti
if(WIN32)
    message(STATUS "Platform: Windows")
    add_compile_definitions(RJ_PLATFORM_WINDOWS)
elseif(APPLE)
    message(STATUS "Platform: macOS")
    add_compile_definitions(RJ_PLATFORM_MACOS)
elseif(UNIX)
    message(STATUS "Platform: Linux")
    add_compile_definitions(RJ_PLATFORM_LINUX)
endif()

# Build turu
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# CRT tutarliligi (Windows)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    add_compile_options(/W4 /WX-)
endif()

# Alt projeler
add_subdirectory(src/pipeline)
add_subdirectory(src/ffi)
add_subdirectory(src/ui)

# Test
enable_testing()
add_subdirectory(tests)

message(STATUS "Reji Studio v${PROJECT_VERSION} yapılandırıldı")