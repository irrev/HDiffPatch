"""本地构建：Windows（x64 + x86）。

宿主机：Windows
依赖：Visual Studio 2019/2022 自带的 cl.exe（脚本依赖 CMakePresets 中的 windows-x64/x86 配置，
      这些 preset 使用 Ninja + cl，因此请在 "Developer Command Prompt for VS"
      或 "x64/x86 Native Tools Command Prompt" 中运行此脚本）。

用法：
    python local_build_lib\\local_build_windows.py [--archs x64,x86]
"""
from __future__ import annotations

import argparse

from _common import BuildTask, LocalBuilder

ARCH_TO_PRESET = {
    "x64": "windows-x64",
    "x86": "windows-x86",
}


def main() -> None:
    p = argparse.ArgumentParser(description="Build HDiffPatch lib for Windows.")
    p.add_argument(
        "--archs",
        default="x64,x86",
        help="Comma-separated subset of: x64,x86 (default: x64,x86)",
    )
    args = p.parse_args()

    archs = [a.strip() for a in args.archs.split(",") if a.strip()]
    unknown = [a for a in archs if a not in ARCH_TO_PRESET]
    if unknown:
        raise SystemExit(f"Unknown arch(s): {unknown}; valid: {list(ARCH_TO_PRESET)}")

    tasks = [BuildTask(preset=ARCH_TO_PRESET[a], arch_label=a) for a in archs]

    LocalBuilder(
        platform_name="windows",
        tasks=tasks,
        needs_bzip2=True,           # Windows 默认 BZIP2 ON（与 CI 一致）
        require_host_os="windows",
    ).run_all()


if __name__ == "__main__":
    main()
