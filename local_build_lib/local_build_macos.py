"""本地构建：macOS（universal: arm64 + x86_64）。

宿主机：macOS（其他系统会被脚本拒绝执行）。
依赖：Xcode 16+。

用法：
    python local_build_lib/local_build_macos.py
"""
from __future__ import annotations

from _common import BuildTask, LocalBuilder


def main() -> None:
    LocalBuilder(
        platform_name="macos",
        tasks=[
            BuildTask(preset="macos", arch_label="universal"),
        ],
        require_host_os="macos",
    ).run_all()


if __name__ == "__main__":
    main()
