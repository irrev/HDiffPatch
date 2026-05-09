# AI 执行工作流（AI 上下文）

本文件给出 AI 在本仓中执行常见任务的推荐"动作剧本"。在不确定如何下手时，优先匹配其中最接近的一条。

---

## 任务 A：新增一个对外 C++ API（供 UE 使用）

1. 在 `build_libs/include/HDiffPatch.h` 中声明函数，使用 `HDIFFPATCH_API` 修饰，放入 `HDiffPatch` 命名空间。
2. 在 `build_libs/source/HDiffPatch.cpp` 中实现，调用 `libHDiffPatch/` 内已有的底层函数；
   **不要直接修改 Layer 1**。
3. 保持接口 **不依赖 UE 类型**；只用标准 C++、`std::vector`、裸指针、基本整型。
4. 在 `UEPlugins/PPakPatcher/Source/PPakPatcher/Public/` 增加一个 UE 友好封装
   （用 `FString`、`TArray<uint8>`、`TSharedPtr` 等）。
5. 更新 `docs/api-reference.md` 和 `docs/build-libs.md`（如启用了新算法/选项）。
6. 如果改动了 `HDiffPatch.h` 的 ABI（函数签名），**提醒用户触发一次 CI 全平台构建验证**。

## 任务 B：新增一个目标平台或架构

必须同步改动（任何一处漏改，都会导致 release zip 缺文件或 UE 链接失败）：

1. `build_libs/CMakePresets.json` 新增预设。
2. `.github/workflows/ci-build-ueplugins.yml` matrix 增加一行 `build_target`，并在 `include:` 里加 `runner` 和 `preset`。
3. 同 CI 文件的 `release` job 的 `copy_lib` 段，增加该平台的库拷贝。
4. `UEPlugins/PPakPatcher/Source/PPakPatcher/PPakPatcher.Build.cs` 增加该平台分支。
5. 更新 `docs/architecture.md`、`docs/ci-pipeline.md`、`docs/build-libs.md` 的平台表。
6. 建议在 PR 描述里贴一次成功的 CI run 链接。

### B.1 HarmonyOS（纯血鸿蒙 / OHOS）—— 阶段 1 已落地

阶段 1（Layer 2 + CI）已完成，参考实现：

- `build_libs/CMakePresets.json`：`harmonyos-arm64`、`harmonyos-x86_64`
- `build_libs/CMakeLists.txt`：`elseif(OHOS) ... HDIFFPATCH_PLATFORM_HARMONYOS`
- `build_libs/include/HDiffPatch.h`：`#ifdef HDIFFPATCH_PLATFORM_HARMONYOS` 导出宏分支
- `.github/workflows/ci-build-ueplugins.yml`：env `OHOS_SDK_VERSION` + "Set up OpenHarmony SDK" 步骤 + Configure (HarmonyOS) 分支 + release `copy_lib`
- `local_build_lib/local_build_harmonyos.py`：本地脚本，自动探测 DevEco Studio 内置 SDK
- 产物路径：`ThirdParty/HDiffPatch/{lib,shared}/harmonyos/{arm64,x86_64}/`

阶段 2（待办）：UE 官方支持鸿蒙 Target 后，在 `PPakPatcher.Build.cs` 加 HarmonyOS 平台分支。
当前 `Build.cs` **不要** 主动加鸿蒙分支，否则 UE 编译会报"未知平台"。

通用注意：

- 默认用 OpenHarmony NDK（开源、CI 免登录）；本地可用 DevEco Studio 内置 SDK
  （`C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\native`）。
- 上游 Layer 1 在 OHOS 上**目前无需 fork-patch**（POSIX + musl 即可工作）。
  若以后需要，按任务 I 流程处理。





## 任务 C：调整压缩算法启用情况

1. 修改 `build_libs/CMakeLists.txt` 顶部的 `option(HDIFFPATCH_USE_*)` 默认值。
2. 或在 CI Configure 步骤通过 `-DHDIFFPATCH_USE_XXX=ON/OFF` 覆盖。
3. 提醒用户：**构建机和运行时必须一致**，否则补丁无法解压。
4. 更新 `docs/build-libs.md` §3 的默认表。

## 任务 D：Commandlet 新增子命令 / 参数

1. 在 `PPakPatcherCommandlet.cpp` 的 `Main` 中通过 `FParse::Param` 增加分支。
2. **参数命名遵循既定语义**（不得随意起新名）：
   - `-Old=<path>` 老资产
   - `-New=<path>` 新资产
   - `-Out=<path>` 输出（补丁或还原的 Pak）
   - `-Patch=<path>` 输入补丁
3. 使用 `CheckFileParams` 获取参数，风格与现有分支一致。
4. 区分职责：
   - **生成类**子命令（如 `CreatePakPatch*`）—— 面向生产，产出补丁给玩家。
   - **自检类**子命令（如 `CheckPakPatch` / `PatchPak`）—— 构建机发版前验证补丁可用性，不下发给玩家。
5. 若发现 `CheckPakPatch` / `PatchPak` 分支仍是占位（内部调 `CreatePakPatch`），
   可在用户确认后，按 §2 的语义用 Layer 2 的 `CheckSingleCompressedDiff` / `PatchSingleStream` 实现真正逻辑。
6. 更新 `docs/commandlet-usage.md` 的参数表与示例。

## 任务 E：Merge 上游 HDiffPatch 新版本

1. `git fetch upstream && git merge upstream/master`（或 rebase）。
2. 冲突高发区：
   - `libHDiffPatch/HDiff/`、`libHDiffPatch/HPatch/` 内部接口改名 → 检查 `build_libs/source/HDiffPatch.cpp`。
   - 第三方库文件列表变化 → 检查 `build_libs/CMakeLists.txt` 的 `file(GLOB ...)` 与枚举列表。
3. 合并完成后 **手动触发一次 CI**，确认 11 平台全部通过。
4. 不要在同一个 commit 里既 merge 上游又修 fork 自己的 bug。

## 任务 F：诊断 UE 链接错误

按顺序排查：

1. CI release zip 是否完整？确认
   `UEPlugins/PPakPatcher/Source/PPakPatcher/ThirdParty/HDiffPatch/{lib,shared}/<platform>/<arch>/`
   是否存在对应文件。
2. 当前 UE Target 的平台/架构是否在 CI matrix 中？
3. `PPakPatcher.Build.cs` 的分支是否覆盖该平台/架构？
4. `HDIFFPATCH_STATIC_LIB` / `HDIFFPATCH_PLATFORM_*` 宏是否正确传入？
5. 再不行，在本地按 `docs/build-libs.md` 手工编译一次对照。

## 任务 G：用户想要"减小 Runtime 包体"

- 优先考虑：在 Runtime 模块链接时，通过宏 `HDIFFPATCH_ENABLE_DIFF=0` 裁掉 diff 端代码
  （当前 `HDiffPatch.h` 已有该宏保护，默认 1）。
- 关闭不必要的压缩算法（`HDIFFPATCH_USE_*=OFF`），但要与构建机生成补丁时保持一致。
- **不要**通过删除源文件来"裁剪"——删了之后同步上游会冲突。

## 任务 H：写新文档或更新文档

- 面向人：放 `docs/`，链接加到 `docs/README.md` 索引中。
- 面向 AI：放 `.codebuddy/rules/`，链接加到 `CODEBUDDY.md` 索引中。
- 两类文档应 **内容互补、不重复长篇**：
  - `docs/` 更注重教学、举例、排版；
  - `.codebuddy/rules/` 更注重规则、边界、条目化。

## 任务 I：最小化修改上游（fork-patch）

当问题**确实**无法在 Layer 2 / Layer 3 规避，而必须修改 Layer 1（`libHDiffPatch/` 等）时，按以下剧本执行：

### I.1 先自检是否真的必要

依次反问自己（若其中一项为"是"，应优先用该方式解决，而不是改 Layer 1）：

1. 能否在 `build_libs/source/HDiffPatch.cpp` 封装一层规避？
2. 能否通过新增编译宏在 `build_libs/CMakeLists.txt` 传入来规避（如 `-D_IS_XXX=0`）？
3. 能否在 UE 层（Layer 3）处理？
4. 是否已经在上游最新 master 被修复？（先检查上游，能 rebase 就 rebase）

### I.2 确实要改时的纪律

1. **先与用户确认**，说清楚：改哪些文件、为什么只能在 Layer 1 改、是否打算上报上游。
2. **范围最小**：只改真正必要的行，不顺手格式化、不重命名、不重排 `#include` 顺序。
3. **打标记**：每处改动的**开头和结尾**加注释，便于 merge upstream 时识别冲突：
   ```cpp
   // [fork-patch begin] <simple reason, e.g. HarmonyOS adapter>
   #if defined(__OHOS__)
       // ...
   #endif
   // [fork-patch end]
   ```
4. **独立 commit**：不要和 fork 自身的功能改动混在一起。commit message 用 `upstream-patch:` 前缀：
   - `upstream-patch: add HarmonyOS branch in file_for_patch.c`
   - `upstream-patch: fix null deref in HDiff when oldSize==0`
5. **文档登记**：在 `docs/upstream-patches.md` 记录一行（如果该文件不存在请创建）：
   - 改动范围（文件:行号）
   - 原因
   - 是否已反馈上游（PR/issue 链接）
6. **PR 声明**：PR 模板里必须勾选"影响 Layer 1"并给出解释。
7. **合并上游时**：merge upstream 后运行一次全量构建 CI，确认 fork-patch 仍然生效且无冲突；
   若冲突，需单独 commit 修复 fork-patch，不要在 merge commit 里直接解决。

### I.3 反例（不要这样做）

- ❌ "顺手"把上游文件换行符从 CRLF 改 LF。
- ❌ 把上游的 `TByte` 改名成 `uint8_t`。
- ❌ 把 fork 自身的功能（新 API、UE 适配）写到 `libHDiffPatch/` 内。
- ❌ 修改 `hdiffz.cpp` / `hpatchz.c`（本 fork 不产出 CLI，这些文件不应成为改动目标）。

---

## 通用准则

- **信息充分再动手**：修改任何 `build_libs/` 或 `UEPlugins/` 下文件前，先读相关源码。
- **最小改动**：不顺手重构；不主动修 lint；不主动升级格式化。
- **双语 README 不动**：上游 README 属上游，fork 专属说明写到 `docs/`。
- **Layer 1 "最小化修改"而非"绝对不改"**：允许因 bug / 平台适配 / 安全问题等必要性最小化修改，
  但必须遵守任务 I 的全部纪律（确认、标记、独立 commit、登记、声明）。
