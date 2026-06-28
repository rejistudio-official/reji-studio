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
    for vswhere in [Path("C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"),
                    Path("C:/Program Files/Microsoft Visual Studio/Installer/vswhere.exe")]:
        if vswhere.exists():
            r = subprocess.run([str(vswhere), "-latest", "-property", "installationPath"],
                               capture_output=True, text=True)
            if r.returncode == 0 and r.stdout.strip():
                p = Path(r.stdout.strip()) / "VC/Auxiliary/Build/vcvars64.bat"
                if p.exists():
                    return p
    for year in ["2026", "2025", "2024", "2022"]:
        for edition in ["Community", "Professional", "Enterprise"]:
            p = Path(f"C:/Program Files/Microsoft Visual Studio/{year}/{edition}"
                     "/VC/Auxiliary/Build/vcvars64.bat")
            if p.exists():
                return p
    return None


def find_winsdk():
    """Return (bin_x64, ucrt_lib_x64, um_lib_x64) for the newest usable Windows SDK, or (None,)*3."""
    kits = Path("C:/Program Files (x86)/Windows Kits/10")
    kits_bin = kits / "bin"
    kits_lib = kits / "Lib"
    if not kits_bin.exists():
        return None, None, None
    versions = sorted(
        (d for d in kits_bin.iterdir() if d.is_dir() and d.name.startswith("10.")),
        reverse=True,
    )
    for ver in versions:
        bin_dir  = kits_bin / ver.name / "x64"
        ucrt_lib = kits_lib / ver.name / "ucrt" / "x64"
        um_lib   = kits_lib / ver.name / "um"   / "x64"
        if (bin_dir / "mt.exe").exists() and (um_lib / "kernel32.lib").exists():
            return bin_dir, ucrt_lib, um_lib
    return None, None, None


def find_qt6():
    """Return the Qt6 MSVC x64 installation base dir, or None."""
    candidates = [
        Path("C:/Qt/6.9.0/msvc2022_64"),
        Path("C:/Qt/6.8.0/msvc2022_64"),
        Path("C:/Qt/6.7.0/msvc2022_64"),
    ]
    for base in candidates:
        if (base / "lib" / "cmake" / "Qt6" / "Qt6Config.cmake").exists():
            return base
    return None


def find_nvenc_sdk():
    """Return the NVENC SDK root dir (containing Interface/nvEncodeAPI.h), or None."""
    candidates = [
        Path("C:/nvenc_sdk"),
        Path("C:/NVENC_SDK/Video_Codec_SDK_13.1.15"),
        Path("C:/Program Files/NVIDIA GPU Computing Toolkit/Video_Codec_SDK"),
        ROOT / "third_party" / "nvenc_sdk",
    ]
    for p in candidates:
        if (p / "Interface" / "nvEncodeAPI.h").exists():
            return p
    return None


def setup_ninja():
    if subprocess.run(["where", "ninja.exe"], capture_output=True).returncode == 0:
        return True
    for year in ["2026", "2025", "2024", "2022"]:
        p = Path(f"C:/Program Files/Microsoft Visual Studio/{year}/Community"
                 "/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja")
        if (p / "ninja.exe").exists():
            os.environ["PATH"] = str(p) + ";" + os.environ.get("PATH", "")
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description="Reji Studio Build")
    ap.add_argument("--target", default="reji_app",
                    choices=["reji_app", "reji_pipeline", "reji_ui", "all"])
    ap.add_argument("--config", default="Release", choices=["Release", "Debug"])
    ap.add_argument("--clean",  action="store_true")
    ap.add_argument("--run",    action="store_true")
    ap.add_argument("--log",    default="run.log")
    args = ap.parse_args()

    vcvars = find_vcvars()
    if not vcvars:
        sys.exit("[build] ERROR: Visual Studio not found — install VS 2022/2026")
    generator = "Ninja" if setup_ninja() else "NMake Makefiles"
    print(f"[build] VS: {vcvars.parts[-5]}  gen: {generator}")

    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    BUILD.mkdir(parents=True, exist_ok=True)

    needs_cfg = args.clean or not (BUILD / "CMakeCache.txt").exists()
    qt6_flag = ""
    nvenc_flag = ""
    if needs_cfg:
        qt6_base = find_qt6()
        if qt6_base:
            qt6_flag = f' -DCMAKE_PREFIX_PATH="{qt6_base}"'
        else:
            print("[build] WARN: Qt6 bulunamadı — UI stub olarak derlenir")
        nvenc_sdk = find_nvenc_sdk()
        if nvenc_sdk:
            nvenc_flag = f' -DNVENC_SDK_PATH="{nvenc_sdk}"'
            print(f"[build] NVENC SDK: {nvenc_sdk}")
        else:
            print("[build] INFO: NVENC SDK bulunamadı — preview-only mode (C:/nvenc_sdk bekleniyor)")
    configure_line = (f'cmake -B "{BUILD}" -G "{generator}" -DCMAKE_BUILD_TYPE={args.config}{qt6_flag}{nvenc_flag} "{ROOT}"'
                      if needs_cfg else "")
    build_line = f'cmake --build "{BUILD}" --target {args.target} --config {args.config}'
    j_flag = " -- -j 8" if generator == "Ninja" else ""

    # Write a temporary .bat so each line sees the previous line's env changes.
    # After vcvars64.bat, %PATH%/%LIB% already contain VS paths — we append SDK
    # bin/lib dirs that vcvars omits when the Windows SDK isn't fully wired up.
    import tempfile
    sdk_bin = sdk_ucrt = sdk_um = None
    if needs_cfg:
        sdk_bin, sdk_ucrt, sdk_um = find_winsdk()
    sdk_path_line = f'set "PATH={sdk_bin};%PATH%"\n' if sdk_bin else ""
    sdk_lib_line  = (f'set "LIB={sdk_ucrt};{sdk_um};%LIB%"\n'
                     if sdk_ucrt and sdk_um else "")
    bat_lines = [
        '@echo off\n',
        f'call "{vcvars}" x64\n',
        'if errorlevel 1 exit /b 1\n',
        sdk_path_line,
        sdk_lib_line,
    ]
    if configure_line:
        bat_lines += [f'{configure_line}\n', 'if errorlevel 1 exit /b 1\n']
    bat_lines.append(f'{build_line}{j_flag}\n')

    with tempfile.NamedTemporaryFile(mode='w', suffix='.bat',
                                     delete=False, dir=ROOT) as bat:
        bat.writelines(bat_lines)
        bat_path = bat.name

    t0 = time.time()
    rc = subprocess.run(["cmd", "/c", bat_path], cwd=ROOT).returncode
    os.unlink(bat_path)
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
