cmake_minimum_required(VERSION 3.20)

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