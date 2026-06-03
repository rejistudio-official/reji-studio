cmake_minimum_required(VERSION 3.20)

# Google Test — auto-download via FetchContent
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

# FFI sinir testi
add_executable(test_ffi_boundary
    test_ffi_boundary.cpp
)

target_link_libraries(test_ffi_boundary
    reji_ffi
)

add_test(NAME ffi_boundary COMMAND test_ffi_boundary)

# Frame profiler testi
add_executable(test_frame_profiler
    test_frame_profiler.cpp
)

target_link_libraries(test_frame_profiler
    gtest gtest_main
    reji_pipeline
)

add_test(NAME FrameProfilerTest COMMAND test_frame_profiler)

# Shader cache testi
add_executable(test_shader_cache
    test_shader_cache.cpp
)

target_link_libraries(test_shader_cache
    gtest gtest_main
    reji_ui
)

add_test(NAME ShaderCacheTest COMMAND test_shader_cache)