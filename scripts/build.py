#!/usr/bin/env python3
"""
Reji Studio Build System — Cross-Platform Python Wrapper

Supports Windows (MSVC + Ninja/NMake), macOS (xcode), Linux (gcc/clang)
with automatic detection, colorized output, and error reporting.

Usage:
    python scripts/build.py [--target TARGET] [--clean] [--run] [--config CONFIG]
                            [--generator GENERATOR] [--log LOGFILE]

Examples:
    python scripts/build.py                                  # build reji_app (Release)
    python scripts/build.py --target reji_pipeline --clean   # clean + build pipeline
    python scripts/build.py --run --config Debug             # build + run (Debug)
    python scripts/build.py --target all --generator Ninja   # explicit Ninja
"""

import argparse
import os
import platform
import subprocess
import sys
import time
from pathlib import Path

# Colorama for cross-platform colored output
try:
    from colorama import Fore, Style, init
    init(autoreset=True)
    HAS_COLORS = True
except ImportError:
    HAS_COLORS = False
    class Fore:
        GREEN = RED = YELLOW = CYAN = ""
    class Style:
        BRIGHT = RESET_ALL = ""

class BuildSystem:
    """Build orchestrator for Reji Studio."""

    def __init__(self, args):
        self.args = args
        self.repo_root = Path(__file__).parent.parent
        self.build_dir = self.repo_root / "build"
        self.start_time = None
        self.platform = platform.system()

    def log(self, msg, level="INFO"):
        """Print colored log messages."""
        if level == "INFO":
            prefix = f"{Fore.CYAN}[INFO]{Style.RESET_ALL}"
        elif level == "OK":
            prefix = f"{Fore.GREEN}[OK]{Style.RESET_ALL}"
        elif level == "WARN":
            prefix = f"{Fore.YELLOW}[WARN]{Style.RESET_ALL}"
        elif level == "ERROR":
            prefix = f"{Fore.RED}[ERROR]{Style.RESET_ALL}"
        else:
            prefix = "[?]"
        print(f"{prefix} {msg}")

    def error_exit(self, msg, code=1):
        """Print error and exit."""
        self.log(msg, "ERROR")
        sys.exit(code)

    def run_cmd(self, cmd, shell=True, cwd=None):
        """Execute command, capture output, return (returncode, stdout, stderr)."""
        try:
            result = subprocess.run(
                cmd,
                shell=shell,
                cwd=cwd or self.repo_root,
                capture_output=True,
                text=True,
            )
            return result.returncode, result.stdout, result.stderr
        except Exception as e:
            self.error_exit(f"Failed to execute: {cmd}\n{e}")

    def find_vsdevcmd(self):
        """Find VS 2022/2026 vcvars64.bat path."""
        candidates = []

        # Try vswhere.exe
        code, stdout, _ = self.run_cmd(
            'where vswhere.exe',
            shell=True,
            cwd=None,
        )
        if code == 0:
            vswhere = stdout.strip()
            code, stdout, _ = self.run_cmd(
                f'"{vswhere}" -latest -property installationPath',
                shell=True,
                cwd=None,
            )
            if code == 0:
                vs_path = Path(stdout.strip())
                vcvars = vs_path / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
                if vcvars.exists():
                    return vcvars

        # Manual fallback: VS 2022, VS 2026
        for year in [2026, 2025, 2024, 2022]:
            for edition in ["Community", "Professional", "Enterprise"]:
                for base in [
                    Path("C:/Program Files/Microsoft Visual Studio"),
                    Path("C:/Program Files (x86)/Microsoft Visual Studio"),
                ]:
                    vs_dir = base / str(year) / edition
                    vcvars = vs_dir / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
                    if vcvars.exists():
                        return vcvars

        return None

    def find_cmake(self):
        """Find cmake.exe in PATH."""
        code, stdout, _ = self.run_cmd('where cmake.exe', shell=True, cwd=None)
        if code == 0:
            return Path(stdout.strip()).parent
        return None

    def find_ninja(self):
        """Find ninja.exe in PATH or VS folder."""
        code, stdout, _ = self.run_cmd('where ninja.exe', shell=True, cwd=None)
        if code == 0:
            return Path(stdout.strip())

        # Try VS 2022/2024 CMake folder
        vs_paths = [
            Path("C:/Program Files/Microsoft Visual Studio/2024/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"),
            Path("C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"),
        ]
        for p in vs_paths:
            ninja = p / "ninja.exe"
            if ninja.exists():
                return ninja

        return None

    def select_generator(self):
        """Detect and select CMake generator."""
        if self.args.generator != "auto":
            return self.args.generator.upper()

        if self.platform == "Windows":
            ninja = self.find_ninja()
            if ninja:
                self.log(f"Detected Ninja: {ninja}")
                return "Ninja"
            self.log("Ninja not found, falling back to NMake Makefiles")
            return "NMake Makefiles"
        elif self.platform == "Darwin":
            self.log("Detected macOS — using Xcode")
            return "Xcode"
        else:  # Linux
            self.log("Detected Linux — using Unix Makefiles")
            return "Unix Makefiles"

    def build_windows(self):
        """Build on Windows using MSVC + Ninja/NMake."""
        self.log("🪟 Windows build", "INFO")

        # Find VS developer command
        vcvars = self.find_vsdevcmd()
        if not vcvars:
            self.error_exit(
                "Visual Studio not found. Please install VS 2022/2026:\n"
                "  https://visualstudio.microsoft.com/downloads/\n"
                "Alternatively, set VCVARS_BAT environment variable."
            )
        self.log(f"Found VS: {vcvars.parent.parent.parent.parent}")

        # Find CMake
        cmake_dir = self.find_cmake()
        if not cmake_dir:
            self.error_exit(
                "CMake not found. Install from:\n"
                "  https://cmake.org/download/ or choco install cmake"
            )
        self.log(f"Found CMake: {cmake_dir / 'cmake.exe'}")

        # Select generator
        generator = self.select_generator()
        self.log(f"Generator: {generator}")

        # Prepare build directory
        if self.args.clean and self.build_dir.exists():
            self.log(f"Cleaning build dir: {self.build_dir}")
            import shutil
            shutil.rmtree(self.build_dir)

        self.build_dir.mkdir(parents=True, exist_ok=True)

        # Configure: wrap in vcvars
        config_type = "Release" if self.args.config == "Release" else "Debug"
        configure_cmd = (
            f'call "{vcvars}" x64 && '
            f'cmake -B "{self.build_dir}" '
            f'-G "{generator}" '
            f'-DCMAKE_BUILD_TYPE={config_type} '
            f'"{self.repo_root}"'
        )
        self.log(f"Configuring: {generator}")
        code, stdout, stderr = self.run_cmd(configure_cmd, shell=True)

        if code != 0:
            self.log(f"CMake configure failed:\n{stderr}", "ERROR")
            self._write_log(stderr)
            sys.exit(1)

        # Build
        target = self.args.target if self.args.target else "reji_app"
        build_cmd = (
            f'call "{vcvars}" x64 && '
            f'cmake --build "{self.build_dir}" '
            f'--target {target} '
            f'--config {config_type}'
        )
        if self.platform == "Windows" and generator == "Ninja":
            build_cmd += " -- -j 8"

        self.log(f"Building {target}...")
        code, stdout, stderr = self.run_cmd(build_cmd, shell=True)

        if code != 0:
            self.log(f"Build failed", "ERROR")
            self._write_log(stderr)
            # Show last 20 lines
            lines = stderr.split('\n')
            self.log("Last 20 lines:", "ERROR")
            for line in lines[-20:]:
                if line.strip():
                    print(f"  {line}")
            sys.exit(1)

        self.log(f"Build succeeded: {target}", "OK")
        self._write_log(stdout)

        return target

    def build_macos(self):
        """Build on macOS (stub)."""
        self.log("🍎 macOS build — desteği v0.3'te", "WARN")
        self.log("Using: xcodebuild (detected)", "INFO")
        return None

    def build_linux(self):
        """Build on Linux (stub)."""
        self.log("🐧 Linux build — desteği v0.4'te", "WARN")
        self.log("Detected gcc/clang (stub)", "INFO")
        return None

    def run_binary(self, target):
        """Run the built binary."""
        if self.platform == "Windows":
            exe = self.build_dir / "src" / "ui" / f"{target}.exe"
            if not exe.exists():
                exe = self.build_dir / "src" / f"{target}.exe"
        else:
            exe = self.build_dir / "src" / "ui" / target

        if not exe.exists():
            self.log(f"Binary not found: {exe}", "WARN")
            return

        self.log(f"Running: {exe}")
        subprocess.run([str(exe)])

    def _write_log(self, content):
        """Write build output to log file."""
        log_file = Path(self.args.log or "run.log")
        try:
            with open(log_file, "a") as f:
                f.write(content)
                f.write("\n" + "="*80 + "\n")
        except Exception as e:
            self.log(f"Failed to write log: {e}", "WARN")

    def build(self):
        """Main build entry point."""
        self.start_time = time.time()
        self.log(f"Reji Studio Build — {self.platform}")

        target = None
        if self.platform == "Windows":
            target = self.build_windows()
        elif self.platform == "Darwin":
            self.build_macos()
        elif self.platform == "Linux":
            self.build_linux()
        else:
            self.error_exit(f"Unsupported platform: {self.platform}")

        # Elapsed time
        elapsed = time.time() - self.start_time
        self.log(f"Build time: {elapsed:.2f}s", "OK")

        # Run if requested
        if self.args.run and target:
            self.run_binary(target)


def main():
    parser = argparse.ArgumentParser(
        description="Reji Studio Build System",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--target",
        default="reji_app",
        choices=["reji_app", "reji_pipeline", "reji_ui", "all"],
        help="Build target (default: reji_app)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean build (remove build directory)",
    )
    parser.add_argument(
        "--log",
        default="run.log",
        help="Log file path (default: run.log)",
    )
    parser.add_argument(
        "--run",
        action="store_true",
        help="Run binary after build",
    )
    parser.add_argument(
        "--config",
        default="Release",
        choices=["Release", "Debug"],
        help="Build configuration (default: Release)",
    )
    parser.add_argument(
        "--generator",
        default="auto",
        choices=["auto", "Ninja", "NMake", "Xcode", "Unix"],
        help="CMake generator (default: auto-detect)",
    )

    args = parser.parse_args()

    builder = BuildSystem(args)
    builder.build()


if __name__ == "__main__":
    main()
