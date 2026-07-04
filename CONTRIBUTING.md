# Contributing to Reji Studio

Thanks for your interest! Reji Studio is at an early stage, which means individual contributions have outsized impact — and also that some areas are still volatile. This guide explains how to contribute effectively.

## TL;DR

1. Pick an issue labeled `good first issue` or `help wanted` (or open one to discuss your idea first)
2. Fork → branch → small, focused commits (conventional commits)
3. Make sure `just build`, `just test`, and `just abi-check` pass
4. Open a PR against `master` — CI must be green, keep PRs small

## Development setup

### Full setup (Windows, dual GPU)

Required for pipeline/GPU work:

- Windows 11, MSVC toolchain (C++17)
- Qt 6.8.0
- Vulkan SDK 1.4
- Rust (stable, edition 2021)
- CMake ≥ 3.19 + Ninja
- [`just`](https://github.com/casey/just): `winget install Casey.Just`

```bash
just build     # Release build (real Vulkan)
just run       # Build and run
just test      # Rust orchestrator tests
```

> ⚠️ Always build from the **x64 Native Tools** command prompt, not PowerShell or Git Bash.

### Mock setup (no dual-GPU hardware needed)

**You do not need special hardware to contribute.** The Vulkan mock preset lets you build and work on the orchestrator, protocol layer, UI logic, and tests on any machine:

```bash
cmake --preset mock
cmake --build --preset mock
```

Great mock-friendly areas: obs-websocket protocol (Rust), rule engine, tests, documentation.

## Where to contribute

| Area | Difficulty | Notes |
|---|---|---|
| obs-websocket requests/events (`src/orchestrator/`) | 🟢 Good entry point | Established pattern, well-tested, mock-friendly |
| Tests (C++ `tests/`, Rust `src/orchestrator/tests/`) | 🟢 Good entry point | Always welcome |
| Documentation | 🟢 Good entry point | Architecture docs, guides |
| Rule engine / self-healing (`rules.rs`, `healing.rs`) | 🟡 Intermediate | Discuss design in an issue first |
| UI (`src/ui/`) | 🟡 Intermediate | Qt6 experience helpful |
| FFI boundary (`src/ffi/`, `ffi.rs`, `cbindgen.toml`) | 🔴 Protected | Maintainer review required — see below |
| GPU interop / capture (`pipeline/gpu/`, `pipeline/capture/`) | 🔴 Protected | Hardware-dependent, volatile |

### Protected areas

The C++ ↔ Rust FFI boundary and hardware-dependent GPU code follow strict ABI and isolation rules. PRs touching these paths require maintainer approval (enforced via CODEOWNERS) and must pass the full verification chain:

```bash
just abi-check       # C++ sizeof vs Rust const_assert cross-check
zig build abi-check  # Zig comptime ABI size verification
just test
```

Key rules: all shared structs are `#[repr(C)]` (enums `#[repr(u32)]`), FFI functions never block and never let panics cross the boundary, and `ffi_auto.h` is generated — never edit it by hand. When in doubt, open an issue before writing code.

## Workflow

### Branches and PRs

- Branch from `master`: `feat/short-description`, `fix/short-description`
- **Keep PRs small and focused** — one logical change per PR. Large refactors should be split into stages (see the Stage 1–9 commits in history for the house style)
- Behavior changes and refactoring never mix in the same commit
- Direct pushes to `master` are disabled; all changes go through PRs with green CI

### Commit messages

We use [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(ws): add GetVersion request handling
fix(pipeline): preserve shutdown order in GpuInteropSubsystem
refactor: extract MetricsSubsystem from Pipeline::Impl — Stage 10
docs: add ADR for tolerant Identify handshake
test: characterization coverage for frame pacer
```

The body should explain *what moved and why*, list touched entry points, and end with a verification line (`Verification: build OK; ctest PipelineIntegration passed`). See recent commits for examples.

### Before opening a PR — checklist

- [ ] `just build` succeeds (or mock preset builds, if that's your target)
- [ ] `just test` passes; C++ changes: `ctest --test-dir build` passes
- [ ] New logic has tests (Rust: `src/orchestrator/tests/`, C++: `tests/`)
- [ ] FFI touched? → `just abi-check` + `zig build abi-check` pass
- [ ] No stray files: build logs, `*.tmp*`, debug output, editor artifacts
- [ ] Commit messages follow the convention

## Architecture ground rules

A few invariants that PRs must respect (see `docs/` for details):

1. **Thin orchestrator:** `pipeline.cpp` delegates to subsystems; it contains no business logic.
2. **Hardware isolation:** hardware-dependent code lives only in `pipeline/gpu/external_memory_bridge.*` and `pipeline/capture/capture_dxgi.*`; the pipeline core talks to them via callbacks, never direct includes.
3. **Tolerant WebSocket handshake:** clients that never send Identify are treated as legacy and kept alive. Do not "fix" this to match the obs-websocket spec — `control.html` depends on it.
4. **Shutdown order is contract:** existing init/release ordering must be preserved unless a PR explicitly targets it.

## Questions and discussion

- **Bugs / features:** open a GitHub Issue
- **Design questions / "should I build this?":** open a GitHub Discussion before investing significant time
- Be kind, be concrete, assume good intent.

## For AI-assisted contributors

This repo is set up for Claude Code: `CLAUDE.md` provides session context and `.claude/skills/` contains procedures for FFI changes, obs-websocket work, and subsystem extraction. If you use Claude Code, these load automatically. Human or AI, the same rules and checklists above apply.

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.
