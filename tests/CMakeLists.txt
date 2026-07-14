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

# ── V8/I4: CPU fallback row-pitch güvenli kopya birim testi ──
# Header-only (pitch_copy.h, D3D11'den bağımsız) — reji_pipeline'a link gerekmez,
# sadece gtest. Sentetik pitch çiftleriyle overrun/satır-kayması yokluğunu doğrular.
add_executable(test_gpu_resource_pitch
    test_gpu_resource_pitch.cpp
)

target_include_directories(test_gpu_resource_pitch PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/capture
)

target_link_libraries(test_gpu_resource_pitch
    gtest gtest_main
)

add_test(NAME GpuResourcePitchTest COMMAND test_gpu_resource_pitch)

# ── V8/I10: paylaşımlı SEH filtresi karar mantığı birim testi ──
# seh_filter.cpp doğrudan derlenir (yalnız windows.h'e bağlı, __try içermez) —
# reji_pipeline/Rust link gerekmez. Gerçek fault tetiklemeden pass-through
# sınıflandırması + AV pencere/eşik valfini doğrular (sentetik now_ms).
add_executable(test_seh_filter
    test_seh_filter.cpp
    ${CMAKE_SOURCE_DIR}/src/pipeline/seh_filter.cpp
)

target_include_directories(test_seh_filter PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/include
)

target_link_libraries(test_seh_filter
    gtest gtest_main
)

add_test(NAME SehFilterTest COMMAND test_seh_filter)

# ── V8/I23: GPU pool round-robin slot ring saf seam testi ──
# slot_ring.h header-only (yalnız reji_constants.h'e bağlı, GPU/Vulkan yok) —
# reji_pipeline/Rust link gerekmez, sadece gtest. Round-robin determinizmi +
# "raporlanan slot == tüketilen slot" değişmezini doğrular.
add_executable(test_slot_ring
    test_slot_ring.cpp
)

target_include_directories(test_slot_ring PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/gpu
)

target_link_libraries(test_slot_ring
    gtest gtest_main
)

add_test(NAME SlotRingTest COMMAND test_slot_ring)

# ── K2: keyed-mutex timeout boundedness invariant ──
# reji_pipeline/Rust/Vulkan link gerekmez — reji_constants.h header-only (inline
# constexpr). Yalnız sabitlerin sonsuz (UINT32_MAX/UINT64_MAX) değerine geri
# dönmediğini kilitler; K2 hang'inin kök nedenini yeniden girmekten korur.
add_executable(test_keyed_mutex_timeout
    test_keyed_mutex_timeout.cpp
)

target_link_libraries(test_keyed_mutex_timeout
    gtest gtest_main
)

add_test(NAME KeyedMutexTimeoutTest COMMAND test_keyed_mutex_timeout)