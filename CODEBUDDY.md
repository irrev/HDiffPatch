# CODEBUDDY.md

> 本文件供 **AI 编码助手（CodeBuddy 等）** 阅读，提供项目级上下文与约束。
> 人类阅读的文档请见 `README.md`（上游）、`README_cn.md`（上游）与 `docs/`（本 fork 新增）。

---

## 1. 最重要的背景：本仓是 fork

- **Upstream**：https://github.com/sisong/HDiffPatch —— 二进制文件/目录 diff & patch 的 C/C++ 库。
- **本 fork 的使命**：把 HDiffPatch 改造成一个跨平台 **UE 引擎插件 `PPakPatcher`**，用于游戏 Pak 资产的增量更新。

**最终目标**（务必牢记）：

1. 将 `PPakPatcher` 作为 UE 插件集成到游戏项目；
2. 在构建机上通过 Commandlet (`UEPlugins/PPakPatcher/Source/PPakPatcherEditor/Private/PPakPatcherCommandlet.cpp`) 为两个版本资产生成补丁；
3. 游戏运行时用补丁把老资产打成新资产。

---

## 2. 三层架构（AI 修改代码前必须知道自己动的是哪一层）

```
Layer 3 : UE 插件层  UEPlugins/PPakPatcher/  （Runtime + Editor 双模块）
            │
            ▼
Layer 2 : 跨平台库层  build_libs/
            - include/HDiffPatch.h  对外 API（C++ namespace HDiffPatch）
            - source/HDiffPatch.cpp 实现
            - CMakeLists.txt + CMakePresets.json 产出 11 个平台的 .a/.so/.lib/.dll/.dylib/.framework
            │
            ▼
Layer 1 : 上游核心算法  libHDiffPatch/ + libParallel/（+ 第三方压缩库）
```

- Layer 1 是 **上游代码，原则上保持不变**，仅在 merge upstream 时同步。
- Layer 2 是 **本 fork 新增**，是 Layer 3 访问 Layer 1 的唯一通道。
- Layer 3 是 **本 fork 新增**，包含 UE 业务逻辑、Commandlet、Runtime 封装。

---

## 3. 关键文件索引

| 关注点 | 路径 |
| ------ | ---- |
| CI 工作流 | `.github/workflows/ci-build-ueplugins.yml` |
| 跨平台库 CMake | `build_libs/CMakeLists.txt` |
| CMake 预设 | `build_libs/CMakePresets.json` |
| 对外 API 头 | `build_libs/include/HDiffPatch.h` |
| 对外 API 实现 | `build_libs/source/HDiffPatch.cpp` |
| UE 插件定义 | `UEPlugins/PPakPatcher/PPakPatcher.uplugin` |
| UE Runtime 模块 | `UEPlugins/PPakPatcher/Source/PPakPatcher/` |
| UE Editor 模块 | `UEPlugins/PPakPatcher/Source/PPakPatcherEditor/` |
| Commandlet 入口 | `UEPlugins/PPakPatcher/Source/PPakPatcherEditor/Private/PPakPatcherCommandlet.cpp` |
| 预编译库目标位置 | `UEPlugins/PPakPatcher/Source/PPakPatcher/ThirdParty/HDiffPatch/` （由 CI 填充） |
| 上游原始 README | `README.md` / `README_cn.md` |
| 本 fork 新文档 | `docs/` |

---

## 4. AI 助手硬性行为约束

1. **Layer 1 上游代码遵循"最小化修改"原则**（`libHDiffPatch/`、`libhsync/`、`libParallel/`、
   `bsdiff_wrapper/`、`vcdiff_wrapper/`、`dirDiffPatch/`、`hdiffz.cpp`、`hpatchz.c`、
   `file_for_patch.*`、`_*.h` 等）：
   - **默认不改**；优先通过 Layer 2 封装、宏开关、接口扩展等方式规避。
   - **遇到 bug 或必要性场景时允许最小化修改**（例如：平台适配导致的编译错误、明显 bug、
     安全问题、新平台所需的宏分支）。
   - 修改前必须向用户确认，并说明："改了哪些文件 / 为什么不能在 Layer 2 解决 / 是否打算上报上游"。
   - 每次 Layer 1 改动都要打上醒目注释标记（如 `// [fork-patch] <reason>`），便于后续 merge upstream 时快速识别。
   - PR 中必须勾选"影响 Layer 1"并给出说明（见 `.github/pull_request_template.md`）。
2. **不要删除或重写 `.codebuddy/` 目录**：该目录是项目数据，非临时缓存。
3. **不要修改 `LICENSE`**。
4. **不要删除 `README.md` / `README_cn.md` / `CHANGELOG.md`**：它们是上游资产，保留以便 merge。
5. **不要提交或修改构建产物**：`testhdiffpatch/*.obj`、`*.tlog`、`*.pdb`、`*.user`、`build/`。
6. **新增功能应放在对应层**：
   - 新增对外 API → `build_libs/include/HDiffPatch.h` + `build_libs/source/HDiffPatch.cpp`；
   - UE 业务封装 → `UEPlugins/PPakPatcher/Source/PPakPatcher/` 或 `PPakPatcherEditor/`；
   - 绝不把 UE 依赖泄漏到 `build_libs/`（那层要能脱离 UE 编译）。
7. **改动 CI 矩阵 / 平台时必须四处同步**：`CMakePresets.json` + CI matrix + CI `copy_lib` 段 +
   `PPakPatcher.Build.cs`。漏改任意一处都会导致 release zip 缺文件或 UE 链接失败。
8. **Commandlet 源码尽量保持稳定、向后兼容参数名**：构建机脚本依赖这些参数。
9. **跨平台宏要配对**：`HDIFFPATCH_PLATFORM_WINDOWS/_ANDROID/_IOS/_LINUX/_MACOS` 与
   `HDIFFPATCH_STATIC_LIB=0/1`、`HDIFFPATCH_EXPORTS` 共同决定符号的导入/导出，不要单改一处。

---

## 5. 常见任务的默认做法

| 任务 | 正确做法 |
| ---- | -------- |
| 用户要求"新增一个 patch API 给 UE 用" | Layer 2 先加 C++ API（裸指针），Layer 3 再用 `FString` / `TArray<uint8>` 包一层 |
| 用户要求"支持新平台" | 先改 `CMakePresets.json` + CI matrix + CI `copy_lib` + `Build.cs`，再验证 |
| 用户要求"减小 Runtime 包体" | 优先考虑通过 `HDIFFPATCH_ENABLE_DIFF=0` 等宏裁掉 diff 端；不要删源文件 |
| 用户要求"看看现在怎么用" | 指向 `docs/` 下相应文档，不要从上游 README 复制 `hdiffz` 命令行指南（那与本 fork 的使用路径无关） |
| 用户报链接错误 | 检查 `ThirdParty/HDiffPatch/{lib,shared}/<platform>/<arch>/` 是否完整，与 CI `copy_lib` 是否一致 |
| 用户要 merge 上游 | 提示冲突高发区在 `libHDiffPatch/` 接口签名，需要相应更新 `build_libs/source/HDiffPatch.cpp` |

---

## 6. 详细规则

更细颗粒度的规则在 `.codebuddy/rules/` 下：

- [`project-overview.md`](./.codebuddy/rules/project-overview.md) —— 项目目标、非目标、与上游的边界、平台矩阵（含 OpenHarmony 规划）
- [`module-map.md`](./.codebuddy/rules/module-map.md) —— 完整模块地图与依赖方向
- [`conventions.md`](./.codebuddy/rules/conventions.md) —— 命名 / 提交 / PR / 跨平台约定
- [`ai-workflow.md`](./.codebuddy/rules/ai-workflow.md) —— 9 个任务剧本（含任务 I：最小化修改上游）
