"""
本地构建脚本公共模块。

职责：
- 准备第三方依赖（克隆到仓库同级目录）
- 包装 cmake configure / build
- 把构建产物（lib / shared）归档到 local_build_lib/out/<platform>/<arch>/
- 提供 LocalBuilder 基类，由各平台脚本继承使用

设计原则：
- 不假设用户机器配置，尽量给清晰错误提示
- 优先用环境变量（与 CI 一致），其次再用平台默认路径
- 仅产出"开发自检"用的库；生产发版仍以 CI 为准
"""

from __future__ import annotations

import os
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

# ---------- 路径常量 ----------
SCRIPT_DIR = Path(__file__).resolve().parent          # local_build_lib/
REPO_ROOT = SCRIPT_DIR.parent                          # HDiffPatch/
BUILD_LIBS_DIR = REPO_ROOT / "build_libs"              # CMake 工程根
PARENT_DIR = REPO_ROOT.parent                          # 第三方依赖兄弟目录
OUTPUT_ROOT = SCRIPT_DIR / "out"                       # 本地脚本统一输出目录
BUILD_ROOT = SCRIPT_DIR / "build"                      # CMake 构建目录
# UE 插件第三方库目录；BuildTask 设置 plugin_*_dir 后产物会同步安装到这里
PLUGIN_THIRDPARTY = (
    REPO_ROOT / "UEPlugins" / "PPakPatcher" / "Source" / "PPakPatcher"
    / "ThirdParty" / "HDiffPatch"
)


# ---------- 第三方依赖（与 CI 一致）----------
THIRD_PARTY_REPOS: dict[str, str] = {
    "libmd5":     "https://github.com/sisong/libmd5.git",
    "xxHash":     "https://github.com/sisong/xxHash.git",
    "lzma":       "https://github.com/sisong/lzma.git",
    "zstd":       "https://github.com/sisong/zstd.git",
    "zlib":       "https://github.com/sisong/zlib.git",
    "libdeflate": "https://github.com/sisong/libdeflate.git",
}

# 仅 Windows 默认开启 BZIP2 时需要
THIRD_PARTY_REPOS_WINDOWS_EXTRA: dict[str, str] = {
    "bzip2": "https://github.com/sisong/bzip2.git",
}


# ---------- 工具函数 ----------
def info(msg: str) -> None:
    print(f"[local-build] {msg}", flush=True)


def warn(msg: str) -> None:
    print(f"[local-build][WARN] {msg}", flush=True)


def die(msg: str, code: int = 1) -> None:
    print(f"[local-build][ERROR] {msg}", file=sys.stderr, flush=True)
    sys.exit(code)


def run(cmd: list[str], cwd: Path | None = None, env: dict[str, str] | None = None,
        check: bool = True) -> int:
    info(f"$ {' '.join(str(c) for c in cmd)}" + (f"   (cwd={cwd})" if cwd else ""))
    proc = subprocess.run(cmd, cwd=str(cwd) if cwd else None, env=env)
    if check and proc.returncode != 0:
        die(f"command failed (exit={proc.returncode}): {' '.join(str(c) for c in cmd)}", proc.returncode)
    return proc.returncode


def ensure_third_party(extra_for_windows: bool = False) -> None:
    """克隆缺失的第三方依赖到仓库同级目录。"""
    repos = dict(THIRD_PARTY_REPOS)
    if extra_for_windows:
        repos.update(THIRD_PARTY_REPOS_WINDOWS_EXTRA)

    for name, url in repos.items():
        dest = PARENT_DIR / name
        if dest.is_dir():
            info(f"third-party: {name} already at {dest}")
            continue
        info(f"third-party: cloning {name} -> {dest}")
        run(["git", "clone", "--depth", "1", url, str(dest)])


def find_cmake() -> str:
    """查找 cmake.exe / cmake，优先 PATH，其次 DevEco Studio 内置。"""
    exe = shutil.which("cmake")
    if exe:
        return exe
    # DevEco Studio 在 Win/macOS 上自带 CMake
    fallback = _deveco_buildtool("cmake")
    if fallback:
        info(f"using bundled CMake from DevEco Studio: {fallback}")
        return fallback
    die("`cmake` not found in PATH and not bundled with DevEco Studio. "
        "Please install CMake >= 3.23.5 (https://cmake.org/download/) "
        "or install DevEco Studio 6+ (which bundles CMake).")
    return ""  # unreachable


def find_ninja() -> str | None:
    """查找 ninja.exe / ninja，优先 PATH，其次 DevEco Studio 内置。
    返回 None 表示未找到（仅在使用 Ninja generator 的 preset 上才需要）。
    """
    exe = shutil.which("ninja")
    if exe:
        return exe
    fallback = _deveco_buildtool("ninja")
    if fallback:
        info(f"using bundled Ninja from DevEco Studio: {fallback}")
        return fallback
    return None


def _deveco_buildtool(name: str) -> str | None:
    """从 DevEco Studio 内置工具链中查找指定的可执行文件（cmake / ninja）。

    探测路径优先级：
      1) $DEVECO_PATH/sdk/default/openharmony/native/build-tools/cmake/bin/<name>
      2) $DEVECO_PATH/sdk/default/hms/native/build-tools/cmake/bin/<name>
      3) 各 OS 默认 DevEco Studio 安装路径
    """
    exe_name = f"{name}.exe" if host_os() == "windows" else name

    deveco_roots: list[Path] = []
    deveco_env = os.environ.get("DEVECO_PATH")
    if deveco_env:
        deveco_roots.append(Path(deveco_env))

    sysname = host_os()
    if sysname == "windows":
        deveco_roots.append(Path(r"C:\Program Files\Huawei\DevEco Studio"))
    elif sysname == "macos":
        deveco_roots.append(Path("/Applications/DevEco-Studio.app/Contents"))

    sub_paths = [
        Path("sdk") / "default" / "openharmony" / "native" / "build-tools" / "cmake" / "bin",
        Path("sdk") / "default" / "hms"         / "native" / "build-tools" / "cmake" / "bin",
    ]

    for root in deveco_roots:
        if not root.is_dir():
            continue
        for sub in sub_paths:
            candidate = root / sub / exe_name
            if candidate.is_file():
                return str(candidate)
    return None


def host_os() -> str:
    s = platform.system().lower()
    if s.startswith("win"):
        return "windows"
    if s == "darwin":
        return "macos"
    return s  # linux / freebsd ...


# ---------- 构建器 ----------
@dataclass
class BuildTask:
    """单个架构的构建任务。"""
    preset: str                       # CMakePresets.json 中的 preset 名称
    arch_label: str                   # 输出目录的架构子目录名（如 "x64"、"arm64"）
    extra_cmake_args: list[str] = field(default_factory=list)
    env_overrides: dict[str, str] = field(default_factory=dict)
    config: str = "Release"           # 多配置生成器使用
    plugin_lib_dir: str | None = None     # 非空则把静态库装到插件 ThirdParty/HDiffPatch/lib/<此>
    plugin_shared_dir: str | None = None  # 非空则把动态库装到插件 ThirdParty/HDiffPatch/shared/<此>


@dataclass
class LocalBuilder:
    platform_name: str                # 输出目录的平台名（"windows"、"OpenHarmony" 等）
    tasks: list[BuildTask]
    needs_bzip2: bool = False         # Windows CMakeLists 默认会用到
    require_host_os: str | None = None  # "windows"/"linux"/"macos" 之一时强制限制宿主机

    def check_host(self) -> None:
        if self.require_host_os and host_os() != self.require_host_os:
            die(
                f"Platform '{self.platform_name}' must be built on '{self.require_host_os}', "
                f"but current host is '{host_os()}'."
            )

    def prepare(self) -> None:
        self.check_host()
        find_cmake()
        ensure_third_party(extra_for_windows=self.needs_bzip2)
        OUTPUT_ROOT.mkdir(parents=True, exist_ok=True)
        BUILD_ROOT.mkdir(parents=True, exist_ok=True)

    def configure(self, task: BuildTask) -> Path:
        build_dir = BUILD_ROOT / task.preset
        env = os.environ.copy()
        env.update(task.env_overrides)

        cmake_cmd = [
            find_cmake(),
            "-S", str(BUILD_LIBS_DIR),
            "-B", str(build_dir),
            f"--preset={task.preset}",
        ]

        # 若 PATH 没有 ninja，但 DevEco Studio 自带，则注入给 CMake，避免
        # "CMake was unable to find a build program corresponding to Ninja"。
        # 对非 Ninja generator 的 preset，CMAKE_MAKE_PROGRAM 也会被忽略，无副作用。
        if not shutil.which("ninja"):
            ninja = find_ninja()
            if ninja:
                cmake_cmd.append(f"-DCMAKE_MAKE_PROGRAM={ninja}")

        cmake_cmd.extend(task.extra_cmake_args)

        run(cmake_cmd, cwd=BUILD_LIBS_DIR, env=env)
        return build_dir

    def build(self, task: BuildTask, build_dir: Path) -> None:
        env = os.environ.copy()
        env.update(task.env_overrides)
        run(
            [find_cmake(), "--build", str(build_dir), "--config", task.config],
            env=env,
        )

    def collect(self, task: BuildTask, build_dir: Path) -> None:
        """把产物拷贝到 local_build_lib/out/<platform>/<lib|shared>/<arch>/"""
        out_lib_dir = OUTPUT_ROOT / self.platform_name / "lib" / task.arch_label
        out_shr_dir = OUTPUT_ROOT / self.platform_name / "shared" / task.arch_label
        out_lib_dir.mkdir(parents=True, exist_ok=True)
        out_shr_dir.mkdir(parents=True, exist_ok=True)

        # CMake 工程把产物分到 build/.../static 与 .../shared
        # 多配置生成器（Xcode/MSBuild）会再嵌套一层 Release/Debug
        candidates_static = [
            build_dir / "static",
            build_dir / "static" / task.config,
        ]
        candidates_shared = [
            build_dir / "shared",
            build_dir / "shared" / task.config,
        ]

        copied_any = False
        for d in candidates_static:
            if d.is_dir():
                copied_any |= _copy_artifacts(d, out_lib_dir)
        for d in candidates_shared:
            if d.is_dir():
                copied_any |= _copy_artifacts(d, out_shr_dir)
        if not copied_any:
            warn(f"No artifacts found for preset '{task.preset}' under {build_dir}.")

        # 同步安装到 UE 插件 ThirdParty 目录，便于直接在引擎里使用
        if task.plugin_lib_dir:
            dst = PLUGIN_THIRDPARTY / "lib" / task.plugin_lib_dir
            dst.mkdir(parents=True, exist_ok=True)
            for d in candidates_static:
                if d.is_dir():
                    _copy_artifacts(d, dst)
            info(f"  installed static -> {dst}")
        if task.plugin_shared_dir:
            dst = PLUGIN_THIRDPARTY / "shared" / task.plugin_shared_dir
            dst.mkdir(parents=True, exist_ok=True)
            for d in candidates_shared:
                if d.is_dir():
                    _copy_artifacts(d, dst)
            info(f"  installed shared -> {dst}")

    def run_all(self) -> None:
        self.prepare()
        for t in self.tasks:
            info(f"=== [{self.platform_name}/{t.arch_label}] preset={t.preset} ===")
            bdir = self.configure(t)
            self.build(t, bdir)
            self.collect(t, bdir)
        info(f"DONE. Artifacts at: {OUTPUT_ROOT / self.platform_name}")


def _copy_artifacts(src_dir: Path, dst_dir: Path) -> bool:
    """把 src_dir 下的库文件 / framework 拷贝到 dst_dir。"""
    copied = False
    for entry in src_dir.iterdir():
        # 库文件常见后缀
        if entry.suffix.lower() in {".dll", ".lib", ".so", ".a", ".dylib"}:
            shutil.copy2(entry, dst_dir / entry.name)
            info(f"  copied {entry.name} -> {dst_dir}")
            copied = True
        # iOS framework / macOS bundle 是目录
        elif entry.is_dir() and entry.suffix.lower() == ".framework":
            target = dst_dir / entry.name
            if target.exists():
                shutil.rmtree(target)
            shutil.copytree(entry, target)
            info(f"  copied {entry.name}/ -> {dst_dir}")
            copied = True
    return copied
