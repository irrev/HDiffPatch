# 项目背景（AI 上下文）

## 1. 身份

- 仓库类型：**Fork**
- 上游：https://github.com/sisong/HDiffPatch
- 上游功能：二进制文件/目录之间的 diff（生成补丁）与 patch（应用补丁），C/C++ 库 + CLI。
- 本 fork 目标：**包装为跨平台 UE 插件 PPakPatcher**，用于游戏 Pak 资产增量更新。

## 2. 最终目标（严格按此对齐回答方向）

1. 将 `PPakPatcher` 集成进 UE 游戏项目。
2. **构建机** 上调用 UE Commandlet（`PPakPatcherCommandlet`）：输入新旧两版资产，产出补丁文件。
3. **玩家运行时** 使用补丁，对老资产打补丁得到新资产。

## 3. 非目标（AI 请勿主动扩展到这些方向）

- **不处理补丁分发 / 下载 / CDN**。这些是上层业务，插件只负责 diff 与 patch 计算。
- **不处理文件元数据**（权限、修改时间、软链接等）。文件被视为字节流。
- **不处理签名加密**。如需要由上层业务自行集成。
- **不引入额外的 UE 第三方插件依赖**（UE 核心 `Core/CoreUObject/Engine` 之外，尽量避免）。
- **不重写上游 HDiffPatch 算法**。如需改算法，请优先反馈给上游。

## 4. 与上游的边界

| 内容 | 归属 | AI 是否可修改 |
| ---- | ---- | ------------- |
| `libHDiffPatch/`、`libhsync/`、`libParallel/`、`dirDiffPatch/`、`bsdiff_wrapper/`、`vcdiff_wrapper/` | 上游 | **最小化修改**（见下方说明） |
| `hdiffz.cpp`、`hpatchz.c`、`file_for_patch.*`、根目录 `_*.h` | 上游 CLI / 内部工具 | **最小化修改** |
| `README.md`、`README_cn.md`、`CHANGELOG.md`、`LICENSE` | 上游 | **否**（fork 专属说明写到 `docs/`） |
| `builds/`（VS / Xcode / Make 工程） | 上游 | **最小化修改**（本 fork 不产出 CLI，通常无需改） |
| `build_libs/` | 本 fork 新增 | 是 |
| `UEPlugins/PPakPatcher/` | 本 fork 新增 | 是 |
| `.github/workflows/ci-build-ueplugins.yml` | 本 fork 新增 | 是 |
| `docs/`、`CODEBUDDY.md`、`.codebuddy/` | 本 fork 新增 | 是 |

**"最小化修改"的含义**：

- 默认不动上游文件；**优先**通过 Layer 2 封装、宏开关、接口扩展等方式规避问题。
- 只有在以下情况允许修改上游：
  1. 明确的 bug（并优先考虑是否应该上报上游修复）；
  2. 新平台适配必须在 Layer 1 加宏分支（例如 HarmonyOS / OHOS 的 `_IS_XXX` 开关）；
  3. 安全问题；
  4. 第三方库 API 变动导致编译失败。
- 修改必须：
  - **范围最小**：只改真正必要的行，不要顺手格式化或重构；
  - **打标记**：每处改动加注释 `// [fork-patch] <简短原因>`，便于将来 merge upstream 时快速识别冲突；
  - **可独立回溯**：建议单独成 commit，commit message 以 `upstream-patch:` 开头；
  - **在 PR 中声明**：勾选"影响 Layer 1"并解释。

## 5. 支持平台矩阵

### 5.1 当前已支持（由 CI 矩阵决定，是事实依据）

- Windows(x64, x86)
- Android(arm64, armeabi, x86, x86_64)
- Linux(arm, x86, x64)
- macOS（主机架构）
- iOS（主机架构）
- **HarmonyOS / 纯血鸿蒙（arm64, x86_64）** ✅ 阶段 1：仅产出 `build_libs` 库，
  Layer 3 `PPakPatcher.Build.cs` 尚未加鸿蒙分支（等待 UE 官方支持）。

### 5.2 规划中

- HarmonyOS UE 集成（阶段 2）：等 UE 官方支持鸿蒙 Target 后，补 `PPakPatcher.Build.cs` 平台分支。
- HarmonyOS armeabi-v7a（如有需求再加）。

> 当用户询问"是否支持 XXX 平台"时：
> - 命中 §5.1 列表 → 回答已支持；
> - 命中 §5.2 → 回答"规划中"；
> - 其他 → 回答"当前未规划"，**不要虚构支持情况**。
