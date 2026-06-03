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

    # Disable manifest file generation (RC.exe / MT.exe missing)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")

    # Windows SDK path (v0.4+ fix for crtdbg.h)
    if(NOT DEFINED WINDOWS_SDK_PATH)
        set(WINDOWS_SDK_PATH "C:/Program Files (x86)/Windows Kits/10")
    endif()

    if(NOT DEFINED WINDOWS_SDK_VERSION)
        set(WINDOWS_SDK_VERSION "10.0.26100.0")
    endif()

    include_directories(
        "${WINDOWS_SDK_PATH}/Include/${WINDOWS_SDK_VERSION}/ucrt"
        "${WINDOWS_SDK_PATH}/Include/${WINDOWS_SDK_VERSION}/um"
        "${WINDOWS_SDK_PATH}/Include/${WINDOWS_SDK_VERSION}/shared"
    )

    link_directories(
        "${WINDOWS_SDK_PATH}/Lib/${WINDOWS_SDK_VERSION}/ucrt/x64"
        "${WINDOWS_SDK_PATH}/Lib/${WINDOWS_SDK_VERSION}/um/x64"
    )
endif()

# Alt projeler
add_subdirectory(src/pipeline)
add_subdirectory(src/ffi)
add_subdirectory(src/ui)

# Test
enable_testing()
add_subdirectory(tests)

message(STATUS "Reji Studio v${PROJECT_VERSION} yapılandırıldı")