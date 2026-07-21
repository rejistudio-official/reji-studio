cmake_minimum_required(VERSION 3.20)

# Google Test — auto-download via FetchContent
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

# Qt6::Core — rules-watch testi QFileSystemWatcher kullanır (headless, GUI yok).
find_package(Qt6 COMPONENTS Core QUIET)

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

# ── Video Ayarları: bitrate REDUCE tabanı clamp saf testi ──
# bitrate_policy.h header-only (yalnız <cstdint>/<algorithm>) — reji_pipeline/Rust
# link gerekmez, sadece gtest. Kullanıcı hedef bitrate'i REDUCE tabanının altına
# indirdiğinde tabanın hedefe clamp'lendiğini (healing sessizce durmasın) kilitler.
add_executable(test_bitrate_policy
    test_bitrate_policy.cpp
)

target_include_directories(test_bitrate_policy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/include
)

target_link_libraries(test_bitrate_policy
    gtest gtest_main
)

add_test(NAME BitratePolicyTest COMMAND test_bitrate_policy)

# ── ISource/ExistingDesktopSource: saf çekirdek testi ──
# desktop_source_logic.h header-only (yalnız i_source.h + i_screen_capture.h,
# D3D11/GPU yok) — reji_pipeline/Rust link gerekmez, sadece gtest. CapturedFrame→
# SourceFrame alan eşlemesini ve null-streak → NeedsReinit eşiğini (60, Capture-
# Subsystem ile birebir) kilitler.
add_executable(test_desktop_source_logic
    test_desktop_source_logic.cpp
)

target_include_directories(test_desktop_source_logic PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/include
)

target_link_libraries(test_desktop_source_logic
    gtest gtest_main
)

add_test(NAME DesktopSourceLogicTest COMMAND test_desktop_source_logic)

# ── ISource/ExistingDesktopSource: adapter yapıştırma testi ──
# Enjekte edilen sahte IScreenCapture ile (test seam) — gerçek GPU/ekran
# gerekmez. existing_desktop_source.cpp reji_pipeline içinde; static lib'ten
# capture/factory obj'leri çekildiği için OutputSubsystemTest link kalıbı.
if(WIN32)
    add_executable(test_existing_desktop_source
        test_existing_desktop_source.cpp
    )

    target_link_libraries(test_existing_desktop_source
        gtest gtest_main
        reji_pipeline
        ${_RUST_ORCH}   # rj_* FFI fonksiyonları + Rust compiler-rt
        ntdll.lib       # Zig/Rust runtime NT API
        userenv.lib     # Rust std::env::home_dir
    )

    add_test(NAME ExistingDesktopSourceTest COMMAND test_existing_desktop_source)

    set_tests_properties(ExistingDesktopSourceTest PROPERTIES
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/src/ui;$ENV{PATH}"
    )
endif()

# ── Ses Ayarlari: AAC AudioSpecificConfig saf uretim testi ──
# aac_config.h header-only (yalniz <cstdint>/<array>/<optional>) — reji_pipeline/
# Rust/Media Foundation link gerekmez, sadece gtest. FLV AAC sequence header'i
# icin gereken ASC baytlarini ve MPEG-4 freq-index eslemesini kilitler.
add_executable(test_aac_config
    test_aac_config.cpp
)

target_include_directories(test_aac_config PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/audio
)

target_link_libraries(test_aac_config
    gtest gtest_main
)

add_test(NAME AacConfigTest COMMAND test_aac_config)

# ── Ses Ayarlari: FLV audio tag + PCM donusumu saf testi ──
# flv_audio_tag.h / pcm_convert.h header-only — reji_pipeline/Rust/MF link
# gerekmez, sadece gtest. FLV AAC tag header baytlarini ve float->S16 clamp'ini kilitler.
add_executable(test_audio_wire
    test_audio_wire.cpp
)

target_include_directories(test_audio_wire PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/audio
)

target_link_libraries(test_audio_wire
    gtest gtest_main
)

add_test(NAME AudioWireTest COMMAND test_audio_wire)

# ── Ses Ayarlari: A-V drift savunma valfi saf testi (I10 deseni) ──
# av_sync.h header-only — reji_pipeline/Rust/MF link gerekmez, sadece gtest.
# Drift esigi + throttle karar mantigini kilitler (sync sessizce kotulesmesin).
add_executable(test_av_sync
    test_av_sync.cpp
)

target_include_directories(test_av_sync PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/audio
)

target_link_libraries(test_av_sync
    gtest gtest_main
)

add_test(NAME AvSyncTest COMMAND test_av_sync)

# ── Ses Ayarlari: SPSC ses ring saf testi ──
# audio_ring.h header-only — reji_pipeline/MF link gerekmez, sadece gtest.
# Push/consume FIFO, doluluk (en yeniyi dusur) ve asiri-boyut reddini kilitler.
add_executable(test_audio_ring
    test_audio_ring.cpp
)

target_include_directories(test_audio_ring PRIVATE
    ${CMAKE_SOURCE_DIR}/src/pipeline/audio
)

target_link_libraries(test_audio_ring
    gtest gtest_main
)

add_test(NAME AudioRingTest COMMAND test_audio_ring)

# ── Ses Ayarlari: MF AAC encoder entegrasyon testi ──
# aac_encoder.cpp dogrudan derlenir (test_seh_filter deseni). AAC encoder MFT
# saf yazilim -> donanimsiz/headless calisir. Media Foundation link'lenir.
if(WIN32)
    add_executable(test_aac_encoder
        test_aac_encoder.cpp
        ${CMAKE_SOURCE_DIR}/src/pipeline/audio/aac_encoder.cpp
    )

    target_include_directories(test_aac_encoder PRIVATE
        ${CMAKE_SOURCE_DIR}/src/pipeline/audio
    )

    target_link_libraries(test_aac_encoder
        gtest gtest_main
        mfplat.lib mfuuid.lib wmcodecdspuuid.lib ole32.lib
    )

    add_test(NAME AacEncoderTest COMMAND test_aac_encoder)

    # ── Ses Ayarlari: WASAPI cihaz enumerasyonu testi ──
    # audio_device_enum.cpp dogrudan derlenir. Donanimsiz calisir (bos liste
    # gecerli). ole32/mmdevapi link.
    add_executable(test_audio_device_enum
        test_audio_device_enum.cpp
        ${CMAKE_SOURCE_DIR}/src/pipeline/audio/audio_device_enum.cpp
    )
    target_include_directories(test_audio_device_enum PRIVATE
        ${CMAKE_SOURCE_DIR}/src/pipeline/audio
    )
    target_link_libraries(test_audio_device_enum
        gtest gtest_main
        ole32.lib
    )
    add_test(NAME AudioDeviceEnumTest COMMAND test_audio_device_enum)
endif()

# ── Donanim Profilleme: suggest_profile saf karar mantigi testi ──
# profile_advisor.cpp dogrudan derlenir (test_seh_filter deseni) — Qt/pipeline/
# Rust link gerekmez, sadece gtest. windows.h (RAM/batarya sorgulari) icin WIN32.
# Yalniz saf suggest_profile test edilir; esik tablosu (VRAM/RAM/batarya) kilitlenir.
add_executable(test_profile_advisor
    test_profile_advisor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/profile_advisor.cpp
)

target_include_directories(test_profile_advisor PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ui
    ${CMAKE_SOURCE_DIR}/src/pipeline/include   # reji_constants.h
)

target_link_libraries(test_profile_advisor
    gtest gtest_main
)

add_test(NAME ProfileAdvisorTest COMMAND test_profile_advisor)

# ── Hot-reload: kuruluş-sırası regresyon kilidi (rules_watch seam) ──
# armRulesWatchOn() saf çekirdeğini (src/ui/rules_watch.h) gerçek
# QFileSystemWatcher ile, kullanıcının yaşadığı sırayla (önce arm/checkbox,
# sonra dosyanın ilk kez oluşması) çalıştırır. Yalnız Qt6::Core gerekir —
# pipeline/Rust/Vulkan link'i yok. Qt6::Core yoksa test atlanır (opsiyonel).
if(Qt6_FOUND)
    add_executable(test_rules_watch
        test_rules_watch.cpp
    )

    target_include_directories(test_rules_watch PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ui
    )

    target_link_libraries(test_rules_watch
        gtest   # not: özel main() var (QCoreApplication kurar) — gtest_main değil
        Qt6::Core
    )

    add_test(NAME RulesWatchTest COMMAND test_rules_watch)

    # ── V10/L1-ek: qrc kaynak kaydı regresyon kilidi (ACIL_L1_QRC_REGRESYON) ──
    # reji_ui.lib'i uygulamayla AYNI şekilde (statik lib) link'ler — qrc
    # nesnesinin linker tarafından atılması ancak bu link biçiminde ürer;
    # qrc'yi test exe'sine gömmek bug'ı temsil ETMEZDİ (test boşluğu buydu).
    add_executable(test_qrc_resources
        test_qrc_resources.cpp
    )
    target_link_libraries(test_qrc_resources
        gtest    # özel main() (QCoreApplication kurar) — gtest_main değil
        reji_ui
        Qt6::Core
    )
    add_test(NAME QrcResourcesTest COMMAND test_qrc_resources)

    # ── SIYAH_KUTU_REGRESYON: HealingOverlay ilk-gösterim geometri kilidi ──
    # onActionEvent, showMessage'dan farklı olarak adjustSize çağırmıyordu →
    # overlay hiç showMessage görmemişse ~30px içeriksiz siyah şerit.
    # reji_ui AUTOMOC'un tek mocs_compilation nesnesi MainWindow'u da
    # sürüklediğinden uygulamanın tam bağımlılık seti link'lenir (reji_app
    # deseni: reji_ffi + Rust lib + Windows runtime bağımlılıkları).
    # Rust lib: src/ui'deki imported-target deseninin test-kapsamı kopyası —
    # build config'ine göre doğru (release/debug) lib seçilir; ham yol
    # RUST_BUILD_TYPE görünürlüğüne takılıp bayat debug lib'e bağlanıyordu.
    add_library(reji_orchestrator_tests STATIC IMPORTED)
    set_target_properties(reji_orchestrator_tests PROPERTIES
        IMPORTED_LOCATION         "${CMAKE_SOURCE_DIR}/target/release/reji_orchestrator.lib"
        IMPORTED_LOCATION_DEBUG   "${CMAKE_SOURCE_DIR}/target/debug/reji_orchestrator.lib"
        IMPORTED_LOCATION_RELEASE "${CMAKE_SOURCE_DIR}/target/release/reji_orchestrator.lib"
    )
    add_executable(test_healing_overlay
        test_healing_overlay.cpp
    )
    target_include_directories(test_healing_overlay PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ui
    )
    target_link_libraries(test_healing_overlay
        gtest    # özel main() (offscreen QApplication) — gtest_main değil
        reji_ui
        reji_ffi
        reji_orchestrator_tests
        ws2_32 bcrypt userenv ntdll
    )
    add_test(NAME HealingOverlayTest COMMAND test_healing_overlay)

    # Qt6Core.dll çalışma zamanında gerekir — Qt bin'ini ctest PATH'ine ekle
    # (diğer testlerdeki build/src/ui deseniyle aynı; DLL_NOT_FOUND'u önler).
    get_target_property(_qt_core_loc Qt6::Core LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_core_loc}" DIRECTORY)
    set_tests_properties(RulesWatchTest QrcResourcesTest HealingOverlayTest PROPERTIES
        ENVIRONMENT "PATH=${_qt_bin_dir};$ENV{PATH}")
else()
    message(STATUS "Qt6::Core bulunamadı — RulesWatchTest atlanıyor")
endif()