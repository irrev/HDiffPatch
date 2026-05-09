"""本地构建：Linux（arm / x86 / x64）。

宿主机：Linux（其他系统会被脚本拒绝执行）。
依赖：clang/clang++ 在 PATH 里；arm/x86 需安装相应交叉编译器。

用法：
    python local_build_lib/local_build_linux.py [--archs arm,x86,x64]
"""
from __future__ import annotations

import argparse

from _common import BuildTask, LocalBuilder

ARCH_TO_PRESET = {
    "arm": "linux-arm",
    "x86": "linux-x86",
    "x64": "linux-x64",
}


def main() -> None:
    p = argparse.ArgumentParser(description="Build HDiffPatch lib for Linux.")
    p.add_argument(
        "--archs",
        default="x64",
        help="Comma-separated subset of: arm,x86,x64 (default: x64)",
    )
    args = p.parse_args()

    archs = [a.strip() for a in args.archs.split(",") if a.strip()]
    unknown = [a for a in archs if a not in ARCH_TO_PRESET]
    if unknown:
        raise SystemExit(f"Unknown arch(s): {unknown}; valid: {list(ARCH_TO_PRESET)}")

    tasks = [BuildTask(preset=ARCH_TO_PRESET[a], arch_label=a) for a in archs]

    LocalBuilder(
        platform_name="linux",
        tasks=tasks,
        require_host_os="linux",
    ).run_all()


if __name__ == "__main__":
    main()
