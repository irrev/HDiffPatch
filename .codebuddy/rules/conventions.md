# 代码与协作约定（AI 上下文）

## 1. 分层纪律（最重要）

| 层 | 允许使用的依赖 |
| ---- | ------------- |
| Layer 1（上游） | C/C++ 标准库、第三方压缩库源码 |
| Layer 2（`build_libs/`） | C/C++ 标准库、Layer 1、第三方压缩库。**禁止 UE 头文件** |
| Layer 3（`UEPlugins/`） | UE API、Layer 2 的 `HDiffPatch.h`、UE 标准模块 |

违反分层约束会导致 `build_libs/` 无法脱离 UE 独立构建，CI 立刻挂掉。

## 2. 命名

### C++（Layer 1 / Layer 2）

- 上游公共 C API 前缀：`hpatch_`、`hdiff_`。
- Layer 2 对外 API 全部位于命名空间 `HDiffPatch`，以 `HDIFFPATCH_API` 修饰。
- 类型前缀：`THDiff...`、`THPatch...`、`hpatch_TStreamInput` 等（保持上游风格）。
- 内部辅助头：`_xxx.h`（下划线开头，不视为对外 API）。
- 宏：全大写带模块前缀，如 `HDIFFPATCH_PLATFORM_*`、`_CompressPlugin_*`、`_ChecksumPlugin_*`。

### UE（Layer 3）

- 遵循 UE 命名规范：`U` 前缀表 UObject，`F` 前缀表 struct/class 普通类型，`A` 前缀表 Actor。
- 插件名 `PPakPatcher`；Runtime 模块宏 `PPAKPATCHER_API`，Editor 模块宏 `PPAKPATCHEREDITOR_API`。
- 日志类目命名：`LogPakPatcher*`，例如 `LogPakPatcherCommandlet`。

## 3. 文件类型

- `.h` / `.c`：纯 C。
- `.cpp`：C++。C++ 函数要提供给 C 调用时需 `extern "C"`（Layer 2 的 `HDIFFPATCH_API` 已含处理）。
- `.mm`：Objective-C++（Apple），尽量避免。
- `.cs`：只用于 UE 的 `*.Build.cs` 模块规则。

## 4. 跨平台

- **路径分隔符**：内部统一 `/`；与 OS API 交互时再转。
- **整数类型**：跨平台接口不要直接使用 `size_t` / `off_t`；大文件偏移用 64 位。
- **宏分支**：Layer 2 使用 `HDIFFPATCH_PLATFORM_*`；Layer 1 使用上游的 `_WIN32`、`__APPLE__` 等；
  请勿混用两套风格。
- **不要假设 `long` 是 64 位**（Windows 上是 32 位）。

## 5. 错误处理

- Layer 2 对外 API：返回 `bool`，非 0 失败；不抛 C++ 异常跨 ABI。
- Layer 3 Commandlet：返回 `int32`，0 成功，非 0 为错误码，构建机据此判定。
- 不使用 `exit()` / `std::terminate()`。

## 6. 内存

- Layer 2 接口以半开区间 `[begin, end)` 传递缓冲，由调用方分配。
- 大文件 patch 尽量使用 stream 接口，避免全量加载。
- Layer 3 UE 封装应使用 UE 的内存管理（`FMemory` / `TArray`），不要直接 `new/delete`。

## 7. 构建与 CI

- 支持的 11 个平台是 **硬约束**。改动任何一个，要同步：
  1. `build_libs/CMakePresets.json`
  2. `.github/workflows/ci-build-ueplugins.yml` 的 matrix
  3. 同 CI 文件中 release job 的 `copy_lib` 调用
  4. `UEPlugins/PPakPatcher/Source/PPakPatcher/PPakPatcher.Build.cs` 的平台/架构分支
- 第三方库以 `../<libname>` 相对路径引用，不要改为绝对路径或本地缓存路径。
- 不要修改 `builds/` 下上游 IDE 工程（那是跑 CLI 用的，本 fork 不需要）。

## 8. 提交规范

- 风格：`<scope>: <subject>`，例如：
  - `build_libs: add option HDIFFPATCH_USE_LDEF default OFF`
  - `UEPlugins: fix Android arm64 link path`
  - `ci: bump NDK to r27`
  - `docs: add runtime-patching guide`
- 主题行 ≤ 72 字符；正文换行宽度 ≤ 100。
- 涉及 public API / 插件接口 / Commandlet 参数 的改动，请同时更新 `docs/` 对应章节。

## 9. PR 规范

- 单一目的：一个 PR 解决一个问题。
- 若涉及 `build_libs/` 或 CI，触发一次 `ci-build-ueplugins.yml`（手动 dispatch）验证 11 平台都能出包。
- 不要提交：`*.obj` / `*.tlog` / `*.pdb` / `*.user` / `build/` / `.vs/` / `Binaries/` / `Intermediate/`。
- 合并上游（rebase / merge upstream）单独成 PR，不要和功能改动混在一起。

## 10. 文档同步

- 新增 Layer 2 API → 更新 `docs/api-reference.md` 与 `docs/build-libs.md`。
- 新增 / 调整 Commandlet 子命令 → 更新 `docs/commandlet-usage.md`。
- 新增目标平台 → 更新 `docs/architecture.md`、`docs/ci-pipeline.md`、`docs/build-libs.md`。
- 上游相关的 `README.md` / `README_cn.md` / `CHANGELOG.md` **不要写 fork 专属内容**，
  所有 fork 新增说明写到 `docs/`。
