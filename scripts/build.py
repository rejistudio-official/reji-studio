#!/usr/bin/env python3
"""Reji Studio build wrapper — Windows MSVC + Ninja/NMake."""

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT  = Path(__file__).parent.parent
BUILD = ROOT / "build"


def find_vcvars():
    vswhere_paths = [
        Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"),
        Path("C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe"),
    ]
    for vswhere in vswhere_paths:
        if not vswhere.exists():
            continue
        r = subprocess.run([str(vswhere), "-latest", "-property", "installationPath"],
                           capture_output=True, text=True)
        if r.returncode == 0 and r.stdout.strip():
            vcvars = Path(r.stdout.strip()) / "VC/Auxiliary/Build/vcvars64.bat"
            if vcvars.exists():
                return vcvars
    for year in ["2026", "2025", "2024", "2022"]:
        for edition in ["Community", "Professional", "Enterprise"]:
            for base in [Path("C:/Program Files"), Path("C:/Program Files (x86)")]:
                p = base / f"Microsoft Visual Studio/{year}/{edition}/VC/Auxiliary/Build/vcvars64.bat"
                if p.exists():
                    return p
    return None


def find_ninja():
    r = subprocess.run(["where", "ninja.exe"], capture_output=True, text=True)
    if r.returncode == 0:
        return True
    for p in [
        Path("C:/Program Files/Microsoft Visual Studio/2024/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"),
        Path("C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja"),
    ]:
        if (p / "ninja.exe").exists():
            os.environ["PATH"] = str(p) + ";" + os.environ.get("PATH", "")
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description="Reji Studio Build")
    ap.add_argument("--target",  default="reji_app",
                    choices=["reji_app", "reji_pipeline", "reji_ui", "all"])
    ap.add_argument("--config",  default="Release", choices=["Release", "Debug"])
    ap.add_argument("--clean",   action="store_true")
    ap.add_argument("--run",     action="store_true")
    ap.add_argument("--log",     default="run.log")
    args = ap.parse_args()

    vcvars = find_vcvars()
    if not vcvars:
        sys.exit("[build] ERROR: Visual Studio not found — install VS 2022/2026")

    generator = "Ninja" if find_ninja() else "NMake Makefiles"
    print(f"[build] VS: {vcvars.parent.parent.parent.parent.name}  gen: {generator}")

    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    BUILD.mkdir(parents=True, exist_ok=True)

    needs_cfg = args.clean or not (BUILD / "CMakeCache.txt").exists()
    configure = (
        f'cmake -B "{BUILD}" -G "{generator}" -DCMAKE_BUILD_TYPE={args.config} "{ROOT}" && '
        if needs_cfg else ""
    )
    j_flag    = " -- -j 8" if generator == "Ninja" else ""
    build_cmd = f'cmake --build "{BUILD}" --target {args.target} --config {args.config}{j_flag}'
    cmd       = f'call "{vcvars}" x64 && {configure}{build_cmd}'

    t0 = time.time()
    rc = subprocess.run(cmd, shell=True, cwd=ROOT).returncode
    elapsed = time.time() - t0

    with open(args.log, "a") as f:
        f.write(f"build {args.target} {args.config} rc={rc} t={elapsed:.1f}s\n")

    if rc != 0:
        sys.exit(f"[build] FAILED ({elapsed:.1f}s)")
    print(f"[build] OK — {args.target} ({elapsed:.1f}s)")

    if args.run:
        for sub in ["src/ui", "src", "."]:
            exe = BUILD / sub / f"{args.target}.exe"
            if exe.exists():
                subprocess.run([str(exe)])
                break
        else:
            print(f"[build] WARN: {args.target}.exe not found under build/")


if __name__ == "__main__":
    main()
