"""本地构建：iOS（arm64）。

宿主机：macOS（其他系统会被脚本拒绝执行）。
依赖：Xcode 16+。

用法：
    python local_build_lib/local_build_ios.py
"""
from __future__ import annotations

from _common import BuildTask, LocalBuilder


def main() -> None:
    LocalBuilder(
        platform_name="ios",
        tasks=[
            BuildTask(preset="ios", arch_label="arm64"),
        ],
        require_host_os="macos",
    ).run_all()


if __name__ == "__main__":
    main()
