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

# Pipeline entegrasyon testi (headless — REJI_VULKAN_MOCK etkin)
add_executable(test_pipeline_integration
    pipeline_integration_test.cpp
)

target_compile_definitions(test_pipeline_integration PRIVATE
    REJI_VULKAN_MOCK=1
)

# Pipeline::init() → rj_start_monitor/rj_metrics_push/… gibi Rust FFI sembollerine
# ve Zig Vulkan wrapper'larına bağlanır. reji_pipeline bunları PRIVATE bağımlılık
# olarak tanımlar; static link'te geçişli aktarım olmaz — açıkça eklenmeli.
#
# Rust orchestrator lib: release varsa kullan, yoksa debug'a düş.
if(EXISTS "${CMAKE_SOURCE_DIR}/target/release/reji_orchestrator.lib")
    set(_RUST_ORCH "${CMAKE_SOURCE_DIR}/target/release/reji_orchestrator.lib")
else()
    set(_RUST_ORCH "${CMAKE_SOURCE_DIR}/target/debug/reji_orchestrator.lib")
endif()

target_link_libraries(test_pipeline_integration
    gtest gtest_main
    reji_pipeline
    ${CMAKE_SOURCE_DIR}/zig-out/lib/vulkan_init_zig.lib
    ${CMAKE_SOURCE_DIR}/zig-out/lib/ext_bridge_zig.lib
    ${_RUST_ORCH}   # rj_* FFI fonksiyonları + Rust compiler-rt (__divti3 vb.)
    ntdll.lib       # Zig runtime NT API: RtlFreeHeap, NtCreateThreadEx vb.
    userenv.lib     # Rust std::env::home_dir → GetUserProfileDirectoryW
)

add_test(NAME PipelineIntegration COMMAND test_pipeline_integration)

# SRT DLL'leri (srt.dll, libcrypto-3-x64.dll, libssl-3-x64.dll) ve diğer
# çalışma zamanı bağımlılıkları build/src/ui/ altında; ctest'e PATH ekle.
set_tests_properties(PipelineIntegration PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/src/ui;$ENV{PATH}"
)

# ── Aşama-0 karakterizasyon harness'i (refactoring regresyon güvenlik ağı) ──
# test_pipeline_integration ile aynı link bağımlılıkları — Pipeline::run_frame()
# rj_* FFI sembollerine ve Zig Vulkan/ext_bridge wrapper'larına bağlanır.
add_executable(test_pipeline_characterization
    pipeline_characterization_test.cpp
)

target_compile_definitions(test_pipeline_characterization PRIVATE
    REJI_VULKAN_MOCK=1
    # baseline_metrics.txt kaynak tests/ dizinine yazılır (build cwd'ye değil).
    REJI_BASELINE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/baseline_metrics.txt"
)

target_link_libraries(test_pipeline_characterization
    gtest gtest_main
    reji_pipeline
    ${CMAKE_SOURCE_DIR}/zig-out/lib/vulkan_init_zig.lib
    ${CMAKE_SOURCE_DIR}/zig-out/lib/ext_bridge_zig.lib
    ${_RUST_ORCH}   # rj_* FFI fonksiyonları + Rust compiler-rt
    ntdll.lib       # Zig runtime NT API
    userenv.lib     # Rust std::env::home_dir
)

add_test(NAME PipelineCharacterization COMMAND test_pipeline_characterization)

set_tests_properties(PipelineCharacterization PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/src/ui;$ENV{PATH}"
)

# ── OutputSubsystem birim testi (Faz2/Aşama1 — ITransport soyutlaması) ──
# Gerçek srt_output.cpp, Rust FFI sembollerine (rj_start_monitor/rj_metrics_push/
# rj_connection_lost) başvurur — integration testleriyle aynı link kalıbı gerekir.
add_executable(test_output_subsystem
    test_output_subsystem.cpp
)

target_link_libraries(test_output_subsystem
    gtest gtest_main
    reji_pipeline
    ${_RUST_ORCH}   # rj_* FFI fonksiyonları + Rust compiler-rt
    ntdll.lib       # Zig/Rust runtime NT API
    userenv.lib     # Rust std::env::home_dir
)

add_test(NAME OutputSubsystemTest COMMAND test_output_subsystem)

# Gerçek SRT build'inde srt.dll çalışma zamanında gerekir (stub build'de zararsız).
set_tests_properties(OutputSubsystemTest PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/src/ui;$ENV{PATH}"
)