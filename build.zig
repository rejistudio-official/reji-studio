// build.zig — Reji Studio Zig build pilot (Zig 0.13.0+)
//
// Kullanım:
//   zig build                                      → zig-out/bin/reji_app.exe
//   zig build -Dvulkan-sdk=C:/VulkanSDK/1.3.290.0 → Vulkan ile tam derleme
//   zig build run -- --headless --frames 10        → headless CI testi
//
// Ön gereksinimler:
//   cargo build --release    → target/release/reji_orchestrator.lib
//   Vulkan SDK (opsiyonel, -Dvulkan-sdk)
//
// Not: QT6_AVAILABLE tanımlanmıyor; Qt6 AUTOMOC adımı gerektirir.
//      reji_app --headless modu Qt6 olmadan çalışır.
// Not: MSVC ABI zorunlu — Rust lib, __try/__except, COM uyumu için.

const std = @import("std");

pub fn build(b: *std.Build) void {
    // x86_64-windows-msvc: Rust lib ve Windows API ile ABI uyumu
    const target = b.resolveTargetQuery(.{
        .cpu_arch = .x86_64,
        .os_tag = .windows,
        .abi = .msvc,
    });
    const optimize = b.standardOptimizeOption(.{});

    const vulkan_sdk = b.option(
        []const u8,
        "vulkan-sdk",
        "Vulkan SDK kök dizini (örn: C:/VulkanSDK/1.3.290.0)",
    ) orelse "";

    const cpp_flags: []const []const u8 = &.{
        "-std=c++17",
        "-DRJ_PLATFORM_WINDOWS",
        "-DWIN32_LEAN_AND_MEAN",
        "-DNOMINMAX",
        "-D_CRT_SECURE_NO_WARNINGS",
    };

    // ── reji_ffi ─────────────────────────────────────────────────────────────
    const ffi_lib = b.addStaticLibrary(.{
        .name = "reji_ffi",
        .target = target,
        .optimize = optimize,
    });
    ffi_lib.addCSourceFile(.{
        .file = b.path("src/ffi/ffi_bridge.c"),
        .flags = &.{},
    });
    ffi_lib.addIncludePath(b.path("src/ffi"));
    ffi_lib.linkLibC();

    // ── reji_pipeline ─────────────────────────────────────────────────────────
    const pipeline_lib = b.addStaticLibrary(.{
        .name = "reji_pipeline",
        .target = target,
        .optimize = optimize,
    });
    // CMakeLists.txt: PIPELINE_SOURCES + WIN32 block ile bire bir eslesme
    pipeline_lib.addCSourceFiles(.{
        .root = b.path("src/pipeline"),
        .files = &.{
            "pipeline.cpp",
            "frame_profiler.cpp",
            "metrics_collector.cpp",
            "copy_optimizer.cpp",
            "frame_pacing.cpp",
            "gpu_query_timing.cpp",
            // WIN32 block
            "capture/capture_dxgi.cpp",
            "capture/gpu_resource_manager.cpp",
            "encode/encode_nvenc.cpp",
            "gpu/vulkan_initializer.cpp",
            "gpu/external_memory_bridge.cpp",
            // WASAPI
            "audio/wasapi_capture.cpp",
            // SRT stub
            "output/srt_output_stub.cpp",
        },
        .flags = cpp_flags,
    });
    pipeline_lib.addIncludePath(b.path("src/pipeline/include"));
    pipeline_lib.addIncludePath(b.path("src/pipeline/capture"));
    pipeline_lib.addIncludePath(b.path("src/pipeline/encode"));
    pipeline_lib.addIncludePath(b.path("src/pipeline/output"));
    pipeline_lib.addIncludePath(b.path("src/pipeline/audio"));
    pipeline_lib.addIncludePath(b.path("src/ffi"));
    if (vulkan_sdk.len > 0) {
        pipeline_lib.addIncludePath(.{
            .cwd_relative = b.fmt("{s}/Include", .{vulkan_sdk}),
        });
    }
    pipeline_lib.linkLibC();
    pipeline_lib.linkLibCpp();
    // CMakeLists.txt: target_link_libraries(reji_pipeline PUBLIC ...)
    pipeline_lib.linkSystemLibrary("d3d11");
    pipeline_lib.linkSystemLibrary("dxgi");
    pipeline_lib.linkSystemLibrary("ole32");
    pipeline_lib.linkSystemLibrary("oleaut32");
    pipeline_lib.linkSystemLibrary("wbemuuid");
    pipeline_lib.linkSystemLibrary("avrt");
    pipeline_lib.linkSystemLibrary("winmm");

    // ── reji_app ──────────────────────────────────────────────────────────────
    const exe = b.addExecutable(.{
        .name = "reji_app",
        .target = target,
        .optimize = optimize,
    });
    exe.addCSourceFile(.{
        .file = b.path("src/ui/main.cpp"),
        .flags = cpp_flags,
    });
    exe.addIncludePath(b.path("src/ui"));
    exe.addIncludePath(b.path("src/pipeline/include"));
    exe.addIncludePath(b.path("src/ffi"));
    if (vulkan_sdk.len > 0) {
        exe.addIncludePath(.{
            .cwd_relative = b.fmt("{s}/Include", .{vulkan_sdk}),
        });
        exe.addLibraryPath(.{
            .cwd_relative = b.fmt("{s}/Lib", .{vulkan_sdk}),
        });
        exe.linkSystemLibrary("vulkan-1");
    }
    exe.linkLibrary(ffi_lib);
    exe.linkLibrary(pipeline_lib);
    exe.linkLibC();
    exe.linkLibCpp();

    // Rust orchestrator: önce `cargo build --release` çalıştırılmalı
    exe.addLibraryPath(b.path("target/release"));
    exe.linkSystemLibrary("reji_orchestrator");
    // Rust Windows runtime bagımlılıkları (src/ui/CMakeLists.txt ile eslesme)
    exe.linkSystemLibrary("ws2_32");
    exe.linkSystemLibrary("bcrypt");
    exe.linkSystemLibrary("userenv");
    exe.linkSystemLibrary("ntdll");

    b.installArtifact(exe);

    // `zig build run -- --headless --frames N`
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);
    const run_step = b.step("run", "reji_app çalıştır (örn: zig build run -- --headless)");
    run_step.dependOn(&run_cmd.step);
}
