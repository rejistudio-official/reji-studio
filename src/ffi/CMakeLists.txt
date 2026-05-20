cmake_minimum_required(VERSION 3.20)
project(reji_ffi C)

add_library(reji_ffi STATIC
    ffi_bridge.c
)

target_include_directories(reji_ffi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
)

if(MSVC)
    set_property(TARGET reji_ffi PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()