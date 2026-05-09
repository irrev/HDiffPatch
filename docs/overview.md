# 项目背景与目标

## 1. 项目由来

本仓库 fork 自 [sisong/HDiffPatch](https://github.com/sisong/HDiffPatch)。

HDiffPatch 上游本身是一个 C/C++ 库 + 命令行工具集，提供二进制文件 / 目录之间的
**diff（生成补丁）** 与 **patch（应用补丁）** 能力，跨平台、补丁体积小、内存占用可控。

## 2. Fork 的目的

将 HDiffPatch 改造为一个 **UE 引擎插件 `PPakPatcher`**，用于游戏 Pak 资产的增量更新。

具体目标：

1. **将 PPakPatcher 集成到 UE 游戏项目中**，作为插件可被各 UE 项目复用。
2. **在游戏构建机上调用 Commandlet** —— `PPakPatcherCommandlet`（位于 `UEPlugins/PPakPatcher/Source/PPakPatcherEditor/Private/PPakPatcherCommandlet.cpp`） —— 对两个版本的资产生成补丁。
3. **在游戏运行时，使用补丁对老资产打补丁**，生成新资产，从而实现资产热更/增量更新。

## 3. 整体数据流

```
              ┌─────────────────────────────────────────────────┐
              │   构建机（Editor 模式 / Commandlet）             │
              │                                                 │
   旧版 Pak ──┤                                                 │
              │  PPakPatcherCommandlet ─> PPakPatcher (Runtime) │── 补丁文件
   新版 Pak ──┤                          └─> HDiffPatch.dll/so  │   (.hdiff)
              │                                                 │
              └─────────────────────────────────────────────────┘

              ┌─────────────────────────────────────────────────┐
              │   游戏运行时（玩家设备：Win/Android/iOS/...）    │
              │                                                 │
   旧版 Pak ──┤                                                 │
              │  PPakPatcher (Runtime) ─> HDiffPatch.dll/so     │── 新版 Pak
   补丁文件 ──┤                                                 │
              │                                                 │
              └─────────────────────────────────────────────────┘
```

## 4. 本仓相对上游新增的内容

| 路径 | 用途 | 由本 fork 新增 |
| ---- | ---- | -------------- |
| `.github/workflows/ci-build-ueplugins.yml` | GitHub CI：在 11 个平台并行构建库并打包成最终 UE 插件 zip | ✓ |
| `build_libs/CMakeLists.txt`、`CMakePresets.json` | 跨平台 CMake 工程，把 HDiffPatch 编译成动态库 / 静态库 | ✓ |
| `build_libs/include/HDiffPatch.h` | 暴露给 UE 项目的对外 API（C++ 命名空间 `HDiffPatch`） | ✓ |
| `build_libs/source/HDiffPatch.cpp` | 上述 API 的实现，封装上游 `libHDiffPatch` | ✓ |
| `UEPlugins/PPakPatcher/` | UE 插件，包含 Runtime 模块与 Editor 模块 | ✓ |

上游原有的 `libHDiffPatch/`、`libhsync/`、`libParallel/`、`hdiffz.cpp`、`hpatchz.c` 等 **保持不变**，本 fork 只通过 `build_libs/` 复用其源码。

## 5. 支持的平台矩阵

由 `ci-build-ueplugins.yml` 决定：

| 平台 | 架构 | 库形态 | 状态 |
| ---- | ---- | ------ | ---- |
| Windows | x64, x86 | `.lib` / `.dll` | ✅ 已支持 |
| Android | arm64, armeabi, x86, x86_64 | `.a` / `.so` | ✅ 已支持 |
| Linux | arm, x86, x64 | `.a` / `.so` | ✅ 已支持 |
| macOS | (主机架构) | `.a` / `.dylib` | ✅ 已支持 |
| iOS | (主机架构) | `.a` / `.framework` | ✅ 已支持 |
| **HarmonyOS（纯血鸿蒙 / OHOS）** | **arm64, x86_64** | **`.a` / `.so`** | ✅ 已支持 |

## 6. 进一步阅读

- 仓库结构：[`architecture.md`](./architecture.md)
- 库构建：[`build-libs.md`](./build-libs.md)
- CI：[`ci-pipeline.md`](./ci-pipeline.md)
- UE 端使用：[`ueplugin-usage.md`](./ueplugin-usage.md)、[`commandlet-usage.md`](./commandlet-usage.md)、[`runtime-patching.md`](./runtime-patching.md)
