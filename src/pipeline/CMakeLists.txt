cmake_minimum_required(VERSION 3.20)
project(reji_pipeline CXX C ASM_MASM)

set(PIPELINE_SOURCES
    pipeline.cpp
    frame_pacer.cpp
    metrics_subsystem.cpp
    output_subsystem.cpp
    command_router.cpp
    frame_profiler.cpp
    metrics_collector.cpp
    copy_optimizer.cpp
    frame_pacing.cpp
    gpu_query_timing.cpp
)

if(WIN32)
    list(APPEND PIPELINE_SOURCES
        capture/capture_dxgi.cpp
        capture/capture_wgc.cpp
        capture/gpu_resource_manager.cpp
        capture_subsystem.cpp
        encode/encode_nvenc.cpp
        encode_subsystem.cpp
        recovery_coordinator.cpp
        screen_capture_factory.cpp
    )

    # Vulkan support (requires Vulkan SDK)
    if(NOT REJI_VULKAN_MOCK)
        list(APPEND PIPELINE_SOURCES
            gpu/vulkan_initializer.cpp
            gpu/external_memory_bridge.cpp
            gpu_interop_subsystem.cpp
            gpu/zig_win32_compat.c
        )
    endif()
endif()

set(PIPELINE_HEADERS
    include/i_screen_capture.h
    include/i_video_encoder.h
    include/i_transport.h
)

add_library(reji_pipeline STATIC ${PIPELINE_SOURCES} ${PIPELINE_HEADERS})

# zig_win32_compat.asm — MASM ayri hedefte derlenir
# ml64.exe CXX flag'lari (/W4 vb.) desteklemiyor; compile kuralini sadeleştir.
if(WIN32 AND NOT REJI_VULKAN_MOCK AND MSVC)
    set(CMAKE_ASM_MASM_COMPILE_OBJECT
        "<CMAKE_ASM_MASM_COMPILER> -c -Fo <OBJECT> <SOURCE>")
    add_library(zig_win32_compat_asm STATIC
        gpu/zig_win32_compat.asm)
    target_link_libraries(reji_pipeline PRIVATE zig_win32_compat_asm)
endif()

target_include_directories(reji_pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/capture
    ${CMAKE_CURRENT_SOURCE_DIR}/encode
    ${CMAKE_CURRENT_SOURCE_DIR}/output
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_SOURCE_DIR}/src/ffi
)

if(WIN32)
    # C++/WinRT headers reside in the Windows SDK cppwinrt/ subfolder which is
    # not added to INCLUDE automatically — locate it via find_path.
    find_path(CPPWINRT_INCLUDE_DIR
        NAMES winrt/Windows.Graphics.Capture.h
        PATHS
            "$ENV{WindowsSdkDir}Include/$ENV{WindowsSDKVersion}cppwinrt"
            "C:/Program Files (x86)/Windows Kits/10/Include/10.0.26100.0/cppwinrt"
            "C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/cppwinrt"
        DOC "Windows SDK cppwinrt include path (required for WGC)"
    )
    if(CPPWINRT_INCLUDE_DIR)
        message(STATUS "C++/WinRT headers: ${CPPWINRT_INCLUDE_DIR}")
        target_include_directories(reji_pipeline PRIVATE ${CPPWINRT_INCLUDE_DIR})
    else()
        message(WARNING "C++/WinRT headers not found — capture_wgc.cpp may fail to compile")
    endif()

    # capture_wgc.cpp and screen_capture_factory.cpp use C++/WinRT (C++20 required).
    # Override just these TUs so the rest of the pipeline stays at C++17.
    if(MSVC)
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/capture/capture_wgc.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/screen_capture_factory.cpp
            PROPERTIES COMPILE_OPTIONS "/std:c++20;/Zc:__cplusplus")
    endif()

    target_link_libraries(reji_pipeline PUBLIC
        d3d11.lib
        dxgi.lib
        ole32.lib
        oleaut32.lib
        wbemuuid.lib
        avrt.lib
        winmm.lib
        windowsapp.lib
    )

    if(Vulkan_FOUND)
        target_link_libraries(reji_pipeline PUBLIC Vulkan::Vulkan)
    endif()

    # Zig FFI lib (zig build ffi ile üretilir)
    target_link_libraries(reji_pipeline PRIVATE
        ${CMAKE_SOURCE_DIR}/zig-out/lib/reji_ffi.lib
    )

    # Zig Vulkan init lib (zig build gpu -Dvulkan-sdk=... ile üretilir)
    target_link_libraries(reji_pipeline PRIVATE
        ${CMAKE_SOURCE_DIR}/zig-out/lib/vulkan_init_zig.lib
    )

    # Zig ExternalMemoryBridge lib (zig build ext-bridge -Dvulkan-sdk=... ile üretilir)
    target_link_libraries(reji_pipeline PRIVATE
        ${CMAKE_SOURCE_DIR}/zig-out/lib/ext_bridge_zig.lib
    )
endif()

# ---------------------------------------------------------------------------
# Optional NVENC support — requires NVIDIA Video Codec SDK 13+
# Download: https://developer.nvidia.com/nvidia-video-codec-sdk
# Extract to C:/nvenc_sdk or set NVENC_SDK_PATH env var / cmake cache variable.
# ---------------------------------------------------------------------------
if(WIN32)
    find_path(NVENC_SDK_PATH Interface/nvEncodeAPI.h
        PATHS
            "C:/nvenc_sdk"
            "$ENV{NVENC_SDK_PATH}"
            "C:/Program Files/NVIDIA GPU Computing Toolkit/Video_Codec_SDK"
        NO_DEFAULT_PATH
    )

    if(NVENC_SDK_PATH)
        target_include_directories(reji_pipeline PRIVATE ${NVENC_SDK_PATH}/Interface)
        target_compile_definitions(reji_pipeline PRIVATE RJ_NVENC_AVAILABLE)
        message(STATUS "[NVENC] SDK found: ${NVENC_SDK_PATH}")
    else()
        message(STATUS "[NVENC] SDK not found — preview-only mode "
                       "(set NVENC_SDK_PATH or extract SDK to C:/nvenc_sdk)")
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
# Vulkan validation layer in Release builds
# cmake -DRJ_VALIDATION=ON  → her zaman aktif (CI, QA ortamları)
# RJ_ENABLE_VULKAN_VALIDATION env var → runtime opt-in (son kullanıcı)
# ---------------------------------------------------------------------------
option(RJ_VALIDATION "Force Vulkan validation layer in Release builds" OFF)
if(RJ_VALIDATION)
    target_compile_definitions(reji_pipeline PRIVATE RJ_VALIDATION)
    message(STATUS "[Vulkan] Validation layer: forced ON (RJ_VALIDATION=ON)")
endif()

# ---------------------------------------------------------------------------
# WASAPI audio capture — Windows only
# ---------------------------------------------------------------------------
if(WIN32)
    target_sources(reji_pipeline PRIVATE audio/wasapi_capture.cpp audio_subsystem.cpp)
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

        # Eski NOTFOUND cache değerlerini temizle ki yeniden arama yapılsın.
        unset(SRT_LIBRARY CACHE)
        unset(SRT_INCLUDE_DIR CACHE)

        # Tüm olası vcpkg konumlarını topla (VS bundled + standalone + argüman).
        set(_srt_lib_hints
            "${VCPKG_ROOT}/installed/x64-windows/lib"
            "${VCPKG_ROOT}/installed/x64-windows-static/lib"
            "C:/vcpkg/installed/x64-windows/lib"
            "C:/vcpkg/installed/x64-windows-static/lib"
        )
        set(_srt_inc_hints
            "${VCPKG_ROOT}/installed/x64-windows/include"
            "${VCPKG_ROOT}/installed/x64-windows-static/include"
            "C:/vcpkg/installed/x64-windows/include"
            "C:/vcpkg/installed/x64-windows-static/include"
        )

        find_library(SRT_LIBRARY
            NAMES srt srt_static
            PATHS ${_srt_lib_hints}
            NO_DEFAULT_PATH
        )
        find_path(SRT_INCLUDE_DIR srt/srt.h
            PATHS ${_srt_inc_hints}
            NO_DEFAULT_PATH
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
        target_sources(reji_pipeline PRIVATE output/srt_output.cpp)
        target_compile_definitions(reji_pipeline PRIVATE RJ_SRT_AVAILABLE)
        target_link_libraries(reji_pipeline PUBLIC SRT::srt ws2_32.lib)
        message(STATUS "SRT output: real module (SRT SDK bulundu)")
    else()
        target_sources(reji_pipeline PRIVATE output/srt_output_stub.cpp)
        message(STATUS "SRT output: stub module (SRT SDK bulunamadi)")
    endif()

    # ITransport faktörü + SrtTransport sarmalayıcı (Faz2/Aşama1) — SDK'dan
    # bağımsız derlenir, o yüzden if/else DIŞINDA: stub build'de de
    # ITransport::create() linklenebilir olmalı. SDK var/yok ayrımı
    # srt_output.cpp / srt_output_stub.cpp seviyesinde çözülür.
    target_sources(reji_pipeline PRIVATE
        output/srt_transport.cpp
        i_transport.cpp
    )

    # RtmpTransport (Faz2/Aşama2.2) — Zig çekirdeği `zig build rtmp` üretir
    # (vulkan_init_zig/ext_bridge_zig kalıbı). librtmp winsock + timeGetTime
    # kullanır → ws2_32 SRT stub build'inde de gerekli (winmm zaten PUBLIC).
    target_sources(reji_pipeline PRIVATE output/rtmp_transport.cpp)
    # legacy_stdio_definitions: MinGW-derlenmiş librtmp obj'leri sscanf/_vsnprintf
    # gibi eski CRT adlarına başvurur; ucrt'de bunlar bu lib ile sağlanır.
    target_link_libraries(reji_pipeline PRIVATE
        ${CMAKE_SOURCE_DIR}/zig-out/lib/rtmp_transport_zig.lib
        legacy_stdio_definitions.lib
    )
    target_link_libraries(reji_pipeline PUBLIC ws2_32.lib)
endif()