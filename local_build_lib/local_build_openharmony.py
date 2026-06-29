"""本地构建：OpenHarmony（arm64 + x86_64）。

宿主机：Linux / macOS / Windows 均可。
依赖：以下任一种 SDK 任选其一，脚本会自动探测：
    1) DevEco Studio（含内置 SDK）
       - Windows 默认: C:\\Program Files\\Huawei\\DevEco Studio\\sdk\\default\\openharmony\\native
       - 也可使用其内置的 hms\\native，脚本会优先选 openharmony
    2) 独立下载的 OpenHarmony NDK
       - GitHub 镜像：https://github.com/openharmony-rs/ohos-sdk/releases
       - 解压后传入 --ndk-home <.../native>

环境变量优先级（高 → 低）：
    --ndk-home 命令行参数
    OHOS_NDK_HOME
    DEVECO_PATH（DevEco Studio 安装根目录，脚本会推导出 sdk/default/openharmony/native）
    Windows 默认安装路径

用法：
    python local_build_lib/local_build_openharmony.py [--archs arm64,x86_64] [--ndk-home <path>]
"""
from __future__ import annotations

import argparse
import os
import platform
from pathlib import Path

from _common import BuildTask, LocalBuilder, info, warn, die

ARCH_TO_PRESET = {
    "arm64":  "openharmony-arm64",
    "x86_64": "openharmony-x86_64",
}


def _candidate_native_dirs() -> list[Path]:
    """按优先级返回可能的 OHOS native 目录候选。"""
    cands: list[Path] = []

    # 1. 显式环境变量
    for ev in ("OHOS_NDK_HOME", "OHOS_SDK_NATIVE", "HMOS_NDK_HOME"):
        v = os.environ.get(ev)
        if v:
            cands.append(Path(v))

    # 2. DevEco Studio 安装根目录 -> sdk/default/{openharmony,hms}/native
    deveco = os.environ.get("DEVECO_PATH")
    if deveco:
        deveco_p = Path(deveco)
        cands.append(deveco_p / "sdk" / "default" / "openharmony" / "native")
        cands.append(deveco_p / "sdk" / "default" / "hms" / "native")

    # 3. 各操作系统的默认 DevEco Studio 安装路径
    sysname = platform.system().lower()
    if sysname.startswith("win"):
        win_defaults = [
            Path(r"C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\native"),
            Path(r"C:\Program Files\Huawei\DevEco Studio\sdk\default\hms\native"),
        ]
        cands.extend(win_defaults)
    elif sysname == "darwin":
        cands.extend([
            Path("/Applications/DevEco-Studio.app/Contents/sdk/default/openharmony/native"),
            Path("/Applications/DevEco-Studio.app/Contents/sdk/default/hms/native"),
        ])
    else:
        pass

    return cands


def resolve_ohos_ndk_home(cli_arg: str | None) -> Path:
    if cli_arg:
        p = Path(cli_arg)
        if not p.is_dir():
            die(f"--ndk-home not exists: {p}")
        return p

    cands = _candidate_native_dirs()
    info("Probing OHOS NDK candidates:")
    for c in cands:
        ok = c.is_dir() and (c / "build" / "cmake" / "ohos.toolchain.cmake").is_file()
        info(f"  {'[OK]' if ok else '[--]'} {c}")
        if ok:
            return c

    # 兜底：如果只有 hmos.toolchain.cmake
    for c in cands:
        if c.is_dir():
            hmos = c / "build" / "cmake" / "hmos.toolchain.cmake"
            if hmos.is_file():
                warn(f"Only hmos.toolchain.cmake found at {c}; will configure with it.")
                return c

    die(
        "OpenHarmony NDK not found.\n"
        "Please either:\n"
        "  - Install DevEco Studio (it bundles SDK at sdk/default/openharmony/native), or\n"
        "  - Download OpenHarmony NDK from https://github.com/openharmony-rs/ohos-sdk/releases\n"
        "    and set OHOS_NDK_HOME to the .../native directory, or\n"
        "  - Pass --ndk-home <path-to-native-dir>"
    )
    return Path()  # unreachable


def pick_toolchain_file(ndk_home: Path) -> Path:
    """优先 ohos.toolchain.cmake，其次 hmos.toolchain.cmake。"""
    cmk_dir = ndk_home / "build" / "cmake"
    candidates = [cmk_dir / "ohos.toolchain.cmake", cmk_dir / "hmos.toolchain.cmake"]
    for c in candidates:
        if c.is_file():
            return c
    die(f"No toolchain file (ohos|hmos).toolchain.cmake under {cmk_dir}")
    return Path()  # unreachable


def main() -> None:
    p = argparse.ArgumentParser(description="Build HDiffPatch lib for OpenHarmony.")
    p.add_argument(
        "--archs",
        default="arm64,x86_64",
        help="Comma-separated subset of: arm64,x86_64 (default: arm64,x86_64)",
    )
    p.add_argument(
        "--ndk-home",
        default=None,
        help="Override: path to OHOS NDK 'native' directory (the dir that contains build/cmake/ohos.toolchain.cmake)",
    )
    args = p.parse_args()

    archs = [a.strip() for a in args.archs.split(",") if a.strip()]
    unknown = [a for a in archs if a not in ARCH_TO_PRESET]
    if unknown:
        raise SystemExit(f"Unknown arch(s): {unknown}; valid: {list(ARCH_TO_PRESET)}")

    ndk_home = resolve_ohos_ndk_home(args.ndk_home)
    toolchain = pick_toolchain_file(ndk_home)
    info(f"OHOS_NDK_HOME = {ndk_home}")
    info(f"toolchain     = {toolchain}")

    env_overrides = {"OHOS_NDK_HOME": str(ndk_home)}
    extra_args = [f"-DCMAKE_TOOLCHAIN_FILE={toolchain}"]

    tasks = [
        BuildTask(
            preset=ARCH_TO_PRESET[a],
            arch_label=a,
            env_overrides=env_overrides,
            extra_cmake_args=extra_args,
            plugin_lib_dir=f"openharmony/{a}",
            plugin_shared_dir=f"openharmony/{a}",
        )
        for a in archs
    ]

    LocalBuilder(
        platform_name="openharmony",
        tasks=tasks,
    ).run_all()


if __name__ == "__main__":
    main()
