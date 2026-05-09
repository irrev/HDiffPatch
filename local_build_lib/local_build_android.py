"""本地构建：Android（arm64 / armeabi / x86 / x86_64）。

宿主机：Linux / macOS / Windows 均可（取决于 Android NDK 是否安装）。
依赖：
    - Android NDK r27（与 CI 一致）
    - 环境变量 NDK_ROOT 指向 NDK 根目录（包含 build/cmake/android.toolchain.cmake）

用法：
    python local_build_lib/local_build_android.py [--archs arm64,armeabi,x86,x86_64]
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path

from _common import BuildTask, LocalBuilder, die

ARCH_TO_PRESET = {
    "arm64":   "android-arm64",
    "armeabi": "android-armeabi",
    "x86":     "android-x86",
    "x86_64":  "android-x86_64",
}


def resolve_ndk_root() -> str:
    candidates = [
        os.environ.get("NDK_ROOT"),
        os.environ.get("NDKROOT"),
        os.environ.get("ANDROID_NDK_HOME"),
        os.environ.get("ANDROID_NDK_ROOT"),
    ]
    for c in candidates:
        if c and Path(c).is_dir():
            return c
    die(
        "Android NDK not found. Please set NDK_ROOT to your NDK directory, e.g.:\n"
        "  Windows:  set NDK_ROOT=C:\\Users\\<you>\\AppData\\Local\\Android\\Sdk\\ndk\\27.0.12077973\n"
        "  Linux:    export NDK_ROOT=$HOME/Android/Sdk/ndk/27.0.12077973"
    )
    return ""  # unreachable


def main() -> None:
    p = argparse.ArgumentParser(description="Build HDiffPatch lib for Android.")
    p.add_argument(
        "--archs",
        default="arm64,armeabi,x86,x86_64",
        help="Comma-separated subset of: arm64,armeabi,x86,x86_64",
    )
    args = p.parse_args()

    archs = [a.strip() for a in args.archs.split(",") if a.strip()]
    unknown = [a for a in archs if a not in ARCH_TO_PRESET]
    if unknown:
        raise SystemExit(f"Unknown arch(s): {unknown}; valid: {list(ARCH_TO_PRESET)}")

    ndk_root = resolve_ndk_root()
    print(f"[local-build] NDK_ROOT = {ndk_root}")
    env_overrides = {"NDK_ROOT": ndk_root}

    tasks = [
        BuildTask(
            preset=ARCH_TO_PRESET[a],
            arch_label=a,
            env_overrides=env_overrides,
        )
        for a in archs
    ]

    LocalBuilder(
        platform_name="android",
        tasks=tasks,
    ).run_all()


if __name__ == "__main__":
    main()
