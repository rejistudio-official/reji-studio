# Reji Studio — task runner
# Install: winget install Casey.Just

set windows-shell := ["cmd", "/c"]

# Default: build
default: build

# Build reji_app (Release)
build:
    python scripts\build.py

# Clean build
rebuild:
    python scripts\build.py --clean

# Build and run
run:
    python scripts\build.py --run

# Rust unit tests
test:
    cargo test --manifest-path src\orchestrator\Cargo.toml

# Fable5 code review
review:
    powershell -NoLogo -File scripts\fable5-review.ps1

# Zamanlanmis tarama: son taramadan 7 gun gecmisse calisir
review-check:
    powershell -NoLogo -File scripts\fable5-review.ps1 -Schedule

# Security shield: Debug build + Fable5 FFI review
shield:
    python scripts\build.py --config Debug
    powershell -NoLogo -File scripts\fable5-review.ps1 -module ffi

# ABI cross-check: C++ sizeof_check.cpp vs Rust const_assert
abi-check:
    powershell -NoLogo -File scripts\check-abi.ps1

# Zig comptime ABI boyut doğrulama
zig-abi:
    zig build abi-check

# Show last 50 lines of run log
log:
    powershell -NoLogo -Command "Get-Content run.log -Tail 50 -ErrorAction SilentlyContinue"
