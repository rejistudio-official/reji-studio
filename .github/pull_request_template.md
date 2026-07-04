## What does this PR do?

<!-- One or two sentences. Link the issue: Closes #123 -->

## Type of change

- [ ] `feat` — new functionality
- [ ] `fix` — bug fix
- [ ] `refactor` — no behavior change (do NOT mix with behavior changes)
- [ ] `test` / `docs` / `chore`

## Areas touched

- [ ] Orchestrator (Rust) — `src/orchestrator/`
- [ ] Pipeline (C++) — `src/pipeline/`
- [ ] UI (Qt6) — `src/ui/`
- [ ] 🔴 **FFI boundary** — `src/ffi/`, `ffi.rs`, `cbindgen.toml` *(CODEOWNERS review + ABI checks required)*
- [ ] 🔴 **GPU/capture (hardware-dependent)** — `pipeline/gpu/`, `pipeline/capture/`

## Verification

<!-- Check what you ran. Mock-only contributors: mark the mock build instead of release. -->

- [ ] `just build` (release) **or** `cmake --preset mock && cmake --build --preset mock`
- [ ] `just test` (Rust tests pass)
- [ ] `ctest --test-dir build` (if C++ changed)
- [ ] `just abi-check` + `zig build abi-check` (**required if FFI touched**)
- [ ] Manual run: no crash, no `HATA` in run.log (if pipeline/UI behavior changed)

## Checklist

- [ ] PR is small and focused (one logical change)
- [ ] New logic has tests
- [ ] Commit messages follow Conventional Commits with a verification line in the body
- [ ] No stray files (build logs, `*.tmp*`, debug output)
- [ ] Architecture invariants respected (thin orchestrator, hardware isolation, shutdown order, tolerant WS handshake) — see CONTRIBUTING.md

## Notes for the reviewer

<!-- Tight couplings kept in the orchestrator? Ordering constraints? Anything non-obvious. -->
