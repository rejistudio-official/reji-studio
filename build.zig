// build.zig — Reji Studio Zig build pilot (Zig 0.16.0)
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
    const ffi_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });
    ffi_mod.addCSourceFile(.{
        .file = b.path("src/ffi/ffi_bridge.c"),
        .flags = &.{},
    });
    ffi_mod.addIncludePath(b.path("src/ffi"));
    const ffi_lib = b.addLibrary(.{
        .name = "reji_ffi",
        .root_module = ffi_mod,
        .linkage = .static,
    });

    // ── reji_pipeline ─────────────────────────────────────────────────────────
    const pipeline_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    // CMakeLists.txt: PIPELINE_SOURCES + WIN32 block ile bire bir eslesme
    pipeline_mod.addCSourceFiles(.{
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
    pipeline_mod.addIncludePath(b.path("src/pipeline/include"));
    pipeline_mod.addIncludePath(b.path("src/pipeline/capture"));
    pipeline_mod.addIncludePath(b.path("src/pipeline/encode"));
    pipeline_mod.addIncludePath(b.path("src/pipeline/output"));
    pipeline_mod.addIncludePath(b.path("src/pipeline/audio"));
    pipeline_mod.addIncludePath(b.path("src/ffi"));
    if (vulkan_sdk.len > 0) {
        pipeline_mod.addIncludePath(.{
            .cwd_relative = b.fmt("{s}/Include", .{vulkan_sdk}),
        });
    }
    // CMakeLists.txt: target_link_libraries(reji_pipeline PUBLIC ...)
    pipeline_mod.linkSystemLibrary("d3d11", .{});
    pipeline_mod.linkSystemLibrary("dxgi", .{});
    pipeline_mod.linkSystemLibrary("ole32", .{});
    pipeline_mod.linkSystemLibrary("oleaut32", .{});
    pipeline_mod.linkSystemLibrary("wbemuuid", .{});
    pipeline_mod.linkSystemLibrary("avrt", .{});
    pipeline_mod.linkSystemLibrary("winmm", .{});
    const pipeline_lib = b.addLibrary(.{
        .name = "reji_pipeline",
        .root_module = pipeline_mod,
        .linkage = .static,
    });

    // ── reji_app ──────────────────────────────────────────────────────────────
    const app_mod = b.createModule(.{
        .target = target,
        .optimize = optimize,
        .link_libc = true,
        .link_libcpp = true,
    });
    app_mod.addCSourceFile(.{
        .file = b.path("src/ui/main.cpp"),
        .flags = cpp_flags,
    });
    app_mod.addIncludePath(b.path("src/ui"));
    app_mod.addIncludePath(b.path("src/pipeline/include"));
    app_mod.addIncludePath(b.path("src/ffi"));
    if (vulkan_sdk.len > 0) {
        app_mod.addIncludePath(.{
            .cwd_relative = b.fmt("{s}/Include", .{vulkan_sdk}),
        });
        app_mod.addLibraryPath(.{
            .cwd_relative = b.fmt("{s}/Lib", .{vulkan_sdk}),
        });
        app_mod.linkSystemLibrary("vulkan-1", .{});
    }
    app_mod.linkLibrary(ffi_lib);
    app_mod.linkLibrary(pipeline_lib);
    // Rust orchestrator: önce `cargo build --release` çalıştırılmalı
    app_mod.addLibraryPath(b.path("target/release"));
    app_mod.linkSystemLibrary("reji_orchestrator", .{});
    // Rust Windows runtime bağımlılıkları (src/ui/CMakeLists.txt ile eslesme)
    app_mod.linkSystemLibrary("ws2_32", .{});
    app_mod.linkSystemLibrary("bcrypt", .{});
    app_mod.linkSystemLibrary("userenv", .{});
    app_mod.linkSystemLibrary("ntdll", .{});
    const exe = b.addExecutable(.{
        .name = "reji_app",
        .root_module = app_mod,
    });

    b.installArtifact(exe);

    // `zig build run -- --headless --frames N`
    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| run_cmd.addArgs(args);
    const run_step = b.step("run", "reji_app çalıştır (örn: zig build run -- --headless)");
    run_step.dependOn(&run_cmd.step);
}
