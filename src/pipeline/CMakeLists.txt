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
    ${CMAKE_SOURCE_DIR}/src/ffi
)

if(WIN32)
    target_link_libraries(reji_pipeline PUBLIC
        d3d11.lib
        dxgi.lib
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
endif()