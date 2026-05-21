cmake_minimum_required(VERSION 3.20)
project(reji_pipeline CXX C)

set(PIPELINE_SOURCES
    pipeline.cpp
)

if(WIN32)
    list(APPEND PIPELINE_SOURCES
        capture/capture_dxgi.cpp
        capture/gpu_resource_manager.cpp
        encode/encode_nvenc.cpp
    )
endif()

add_library(reji_pipeline STATIC ${PIPELINE_SOURCES})

target_include_directories(reji_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/capture
    ${CMAKE_CURRENT_SOURCE_DIR}/encode
    ${CMAKE_CURRENT_SOURCE_DIR}/output
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_SOURCE_DIR}/src/ffi
)

if(WIN32)
    target_link_libraries(reji_pipeline PUBLIC
        d3d11.lib
        dxgi.lib
        ole32.lib
        avrt.lib
        winmm.lib
    )
endif()

# ---------------------------------------------------------------------------
# Optional NVENC support — requires NVIDIA Video Codec SDK
# Set NVENC_SDK_PATH to the SDK root, or put nvEncodeAPI.h in CUDA include dir.
# ---------------------------------------------------------------------------
if(WIN32)
    find_path(NVENC_INCLUDE_DIR nvEncodeAPI.h
        PATHS
            "${NVENC_SDK_PATH}/Interface"
            "${NVENC_SDK_PATH}/include"
            "$ENV{NVENC_SDK_PATH}/Interface"
            "$ENV{NVENC_SDK_PATH}/include"
            "$ENV{CUDA_PATH}/include"
            "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.6/include"
            "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.0/include"
    )

    if(NVENC_INCLUDE_DIR)
        message(STATUS "NVENC SDK found: ${NVENC_INCLUDE_DIR}")
        target_compile_definitions(reji_pipeline PRIVATE RJ_NVENC_AVAILABLE)
        target_include_directories(reji_pipeline PRIVATE ${NVENC_INCLUDE_DIR})
    else()
        message(STATUS "NVENC SDK not found — encode_nvenc.cpp builds as stub. "
                       "Set NVENC_SDK_PATH to enable.")
    endif()
endif()

if(MSVC)
    set_property(TARGET reji_pipeline PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    # /EHa: SEH + C++ exception interop — WASAPI ve SRT modülleri için zorunlu.
    # /EHa, /EHsc'nin üst kümesidir; mevcut kaynaklarla uyumlu.
    target_compile_options(reji_pipeline PRIVATE /EHa)
endif()

# ---------------------------------------------------------------------------
# WASAPI audio capture — Windows only
# ---------------------------------------------------------------------------
if(WIN32)
    target_sources(reji_pipeline PRIVATE audio/wasapi_capture.cpp)
    message(STATUS "WASAPI audio capture modülü etkinleştirildi")
endif()

# ---------------------------------------------------------------------------
# Optional SRT output support — requires libsrt (vcpkg: libsrt)
# Önerilen: cmake -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
# ---------------------------------------------------------------------------
if(WIN32)
    find_package(SRT CONFIG QUIET)

    if(NOT SRT_FOUND AND NOT TARGET SRT::srt)
        # vcpkg toolchain olmadan manuel arama — VCPKG_ROOT env veya argümanla.
        if(NOT DEFINED VCPKG_ROOT)
            set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
        endif()
        if(NOT VCPKG_ROOT)
            set(VCPKG_ROOT "C:/vcpkg")
        endif()

        find_library(SRT_LIBRARY
            NAMES srt srt_static
            PATHS
                "${VCPKG_ROOT}/installed/x64-windows/lib"
                "${VCPKG_ROOT}/installed/x64-windows-static/lib"
        )
        find_path(SRT_INCLUDE_DIR srt/srt.h
            PATHS
                "${VCPKG_ROOT}/installed/x64-windows/include"
                "${VCPKG_ROOT}/installed/x64-windows-static/include"
        )

        if(SRT_LIBRARY AND SRT_INCLUDE_DIR)
            add_library(SRT::srt UNKNOWN IMPORTED)
            set_target_properties(SRT::srt PROPERTIES
                IMPORTED_LOCATION "${SRT_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${SRT_INCLUDE_DIR}"
            )
            set(SRT_FOUND TRUE)
            message(STATUS "SRT found (manual vcpkg): ${SRT_LIBRARY}")
        endif()
    endif()

    if(SRT_FOUND OR TARGET SRT::srt)
        message(STATUS "SRT output module enabled")
        target_sources(reji_pipeline PRIVATE output/srt_output.cpp)
        target_compile_definitions(reji_pipeline PRIVATE RJ_SRT_AVAILABLE)
        target_link_libraries(reji_pipeline PUBLIC SRT::srt ws2_32.lib)
    else()
        message(STATUS "SRT not found — srt_output.cpp disabled. "
                       "Install: vcpkg install libsrt:x64-windows")
    endif()
endif()