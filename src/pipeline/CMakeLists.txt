cmake_minimum_required(VERSION 3.20)
project(reji_pipeline CXX C)

set(PIPELINE_SOURCES
    pipeline.cpp
)

# Platform dosyalari v0.1'de eklenecek
# if(WIN32)
#     list(APPEND PIPELINE_SOURCES
#         capture/capture_dxgi.cpp
#         encode/encode_nvenc.cpp
#     )
# endif()

add_library(reji_pipeline STATIC ${PIPELINE_SOURCES})

target_include_directories(reji_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/ffi
)

if(MSVC)
    set_property(TARGET reji_pipeline PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()