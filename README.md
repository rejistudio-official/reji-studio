<div align="center">

# Reji Studio

**Open-source professional live streaming software**

*Hybrid C++/Rust architecture · Zero-copy dual-GPU pipeline · Self-healing orchestration*

![Status](https://img.shields.io/badge/status-early%20development-orange)
![Version](https://img.shields.io/badge/version-v0.5.2--dev-blue)
![Platform](https://img.shields.io/badge/platform-Windows%2011-informational)
![License](https://img.shields.io/badge/license-Apache--2.0-green)

</div>

---

> ⚠️ **Early-stage project.** Reji Studio is under active development and is **not yet usable for production streaming**. The encode (NVENC) and output (SRT) layers are currently stubs. Follow the repo to track progress.

## What is Reji Studio?

Reji Studio is a from-scratch live streaming application designed around modern GPU architectures. Instead of treating multi-GPU laptops and desktops as an afterthought, it splits work deliberately:

- **Display/render** on the integrated GPU (Vulkan)
- **Capture & encode** on the discrete GPU (D3D11 + NVENC)
- **Zero-copy interop** between the two via external memory — no CPU round-trips

A Rust/Tokio orchestrator supervises the C++ pipeline with an event bus, a rule engine, and self-healing recovery (device-lost/TDR handling), and speaks the **obs-websocket protocol** for remote control compatibility.

## Architecture

```
src/
├── pipeline/gpu/       # D3D11 ↔ Vulkan zero-copy (external memory bridge)
├── pipeline/capture/   # DXGI Desktop Duplication + WGC
├── pipeline/audio/     # WASAPI capture
├── pipeline/encode/    # NVENC (stub — SDK required)
├── pipeline/output/    # SRT (stub — SDK required)
├── ui/                 # Qt6 — preview/program widgets, healing overlay
├── ffi/                # C ABI bridge between C++ and Rust
└── orchestrator/       # Rust/Tokio — event bus, rule engine, self-healing,
                        # obs-websocket server
```

**Design principles**

- Hardware-dependent code is isolated to two modules (`gpu/external_memory_bridge`, `capture/capture_dxgi`); the pipeline core talks to them only through callbacks.
- The C++ ↔ Rust boundary is a stable C ABI, cross-checked at build time (`just abi-check`, Zig comptime verification).
- Every subsystem (capture, encode, GPU interop, metrics, recovery, command routing) is an independently testable unit extracted from a thin pipeline orchestrator.

## Tech stack

| Layer | Technology |
|---|---|
| UI | Qt 6.8 (Widgets + QRhi) |
| Render | Vulkan 1.4 |
| Capture | DXGI Desktop Duplication, Windows Graphics Capture, WASAPI |
| Encode | NVENC (planned) |
| Output | SRT (planned) |
| Orchestration | Rust, Tokio, axum, redb |
| Build | CMake ≥ 3.19 (Ninja presets), Cargo, just |

## Requirements

- Windows 11
- MSVC toolchain (C++17)
- Qt 6.8.0
- Vulkan SDK 1.4
- Rust (stable, edition 2021)
- CMake ≥ 3.19 + Ninja
- [`just`](https://github.com/casey/just) task runner (`winget install Casey.Just`)
- Reference hardware: dual-GPU system (tested on AMD Radeon 780M + NVIDIA RTX 4070 Laptop)

> A **Vulkan mock** build preset exists for development on machines without the full GPU setup.

## Building

```bash
# Release build (real Vulkan)
just build          # → python scripts/build.py

# Build and run
just run

# Clean rebuild
just rebuild

# Debug build with Vulkan mock (no dual-GPU required)
cmake --preset mock
cmake --build --preset mock
```

## Testing

```bash
# Rust orchestrator tests (rules engine, obs-websocket protocol)
just test

# C++ pipeline tests (characterization, integration, FFI boundary,
# frame pacing, GPU timing, shader cache)
cmake --preset release && ctest --test-dir build

# ABI cross-check between C++ and Rust
just abi-check
```

## Roadmap

- [x] **Phase 0** — Pipeline modularization: subsystem extraction, recovery coordinator, thin orchestrator core
- [ ] **Phase 1** — obs-websocket protocol: Hello/Identify handshake ✅, request/event coverage *(in progress)*
- [ ] NVENC encode integration
- [ ] SRT output
- [ ] Scene composition & sources
- [ ] Cross-platform exploration (Linux)

## Contributing

The project is at a very early stage, but issues, ideas, and discussion are welcome — and **you don't need dual-GPU hardware**: a Vulkan mock build preset lets you contribute to the orchestrator, protocol layer, and tests on any machine.

Please read [CONTRIBUTING.md](CONTRIBUTING.md) for setup, workflow, commit conventions, and which areas are open vs. protected.

## License

Licensed under the [Apache License 2.0](LICENSE).
