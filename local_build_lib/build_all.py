"""一键构建：在当前宿主机上跑所有可跑的平台。

策略：
- Windows: windows + android + harmonyos
- Linux:   linux   + android + harmonyos
- macOS:   macos + ios + android + harmonyos

任一平台失败不中断，全部跑完后汇总成功 / 失败列表（与 CI 的 fail-fast: false 一致）。

用法：
    python local_build_lib/build_all.py
"""
from __future__ import annotations

import importlib
import sys
from pathlib import Path

from _common import host_os, info

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))


def run_module(mod_name: str) -> tuple[str, bool, str]:
    try:
        m = importlib.import_module(mod_name)
        m.main()
        return mod_name, True, ""
    except SystemExit as e:
        ok = (e.code in (0, None))
        return mod_name, ok, "" if ok else f"exit={e.code}"
    except Exception as e:  # noqa: BLE001
        return mod_name, False, repr(e)


def main() -> None:
    host = host_os()
    plan: list[str]
    if host == "windows":
        plan = ["local_build_windows", "local_build_android", "local_build_harmonyos"]
    elif host == "linux":
        plan = ["local_build_linux", "local_build_android", "local_build_harmonyos"]
    elif host == "macos":
        plan = [
            "local_build_macos",
            "local_build_ios",
            "local_build_android",
            "local_build_harmonyos",
        ]
    else:
        print(f"Unsupported host: {host}", file=sys.stderr)
        sys.exit(2)

    info(f"host = {host}; plan = {plan}")
    results: list[tuple[str, bool, str]] = []
    for mod in plan:
        # 每个子构建用一个新进程更稳，但这里直接 import 跑足够用
        # 子进程方式可改为 subprocess.run([sys.executable, mod + '.py'])
        info(f"===== {mod} =====")
        results.append(run_module(mod))

    print("\n========== SUMMARY ==========")
    for name, ok, msg in results:
        flag = "OK " if ok else "FAIL"
        extra = f"  ({msg})" if msg else ""
        print(f"  [{flag}] {name}{extra}")

    if any(not ok for _, ok, _ in results):
        sys.exit(1)


if __name__ == "__main__":
    main()
