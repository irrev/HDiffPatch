<!--
感谢贡献 PPakPatcher！请按本模板填写 PR，方便 reviewer 快速理解改动。
与本仓无关的段落可以删除，但请勿整体移除模板。
-->

## 1. 改动概述

<!-- 一句话说明这个 PR 做了什么、为什么做 -->



## 2. 改动类型

<!-- 勾选所有适用项（将 [ ] 改为 [x]） -->

- [ ] 🆕 新增功能 / API
- [ ] 🐛 Bug 修复
- [ ] ♻️ 重构（不改外部行为）
- [ ] ⚙️ 构建 / CI
- [ ] 📝 文档
- [ ] ⬆️ 合并 / 跟随上游 HDiffPatch
- [ ] 其他：

## 3. 影响的层级

<!-- 勾选本 PR 涉及的层。注意分层纪律见 .codebuddy/rules/conventions.md §1 -->

- [ ] **Layer 1** 上游算法 `libHDiffPatch/` `libhsync/` `libParallel/` `dirDiffPatch/` `bsdiff_wrapper/` `vcdiff_wrapper/` 等 ⚠️ *最小化修改，需遵守 fork-patch 纪律*
- [ ] **Layer 2** 跨平台库 `build_libs/`
- [ ] **Layer 3** UE 插件 `UEPlugins/PPakPatcher/`
- [ ] **CI** `.github/workflows/`
- [ ] **文档** `docs/` / `CODEBUDDY.md` / `.codebuddy/`

> ⚠️ 勾选了 Layer 1 时，必须同时勾选下方 §4.G 的 fork-patch checklist，并说明非改不可的原因。

---

## 4. 同步 Checklist（请按改动类型勾选对应段）

### A. 新增对外 C++ API（Layer 2）

- [ ] `build_libs/include/HDiffPatch.h` 已增加声明，使用 `HDIFFPATCH_API` 修饰，放入 `HDiffPatch` 命名空间
- [ ] `build_libs/source/HDiffPatch.cpp` 已提供实现
- [ ] 接口**未依赖** UE 类型（`FString` / `TArray` 等不应出现在 Layer 2）
- [ ] `UEPlugins/PPakPatcher/Source/PPakPatcher/Public|Private/` 提供了 UE 友好封装（如需要）
- [ ] `docs/api-reference.md` 已更新

### B. 新增目标平台 / 架构

- [ ] `build_libs/CMakePresets.json` 已添加预设
- [ ] `.github/workflows/ci-build-ueplugins.yml` **matrix** 增加了 `build_target` / `runner` / `preset`
- [ ] `.github/workflows/ci-build-ueplugins.yml` **release job 的 `copy_lib` 段** 增加了对应平台库的拷贝
- [ ] `UEPlugins/PPakPatcher/Source/PPakPatcher/PPakPatcher.Build.cs` 的平台/架构分支已覆盖
- [ ] `docs/architecture.md` / `docs/ci-pipeline.md` / `docs/build-libs.md` 的平台表已更新
- [ ] 已触发一次 CI 手动 dispatch，**对应平台全部成功**（贴链接 👇）
  - CI run：

#### B.1 若为 OpenHarmony（纯血鸿蒙 / OHOS）

- [ ] CI 中已加入 OHOS NDK 的安装步骤（参考现有 Android NDK 处理方式）
- [ ] `CMakePresets.json` 使用了 OHOS NDK 的 toolchain 文件（`ohos.toolchain.cmake` 或同等）
- [ ] `build_libs/CMakeLists.txt` 新增 `elseif(OHOS) ...` 分支，定义 `HDIFFPATCH_PLATFORM_OPENHARMONY`
- [ ] `build_libs/include/HDiffPatch.h` 新增 `#ifdef HDIFFPATCH_PLATFORM_OPENHARMONY` 的导出宏分支
- [ ] 产物目录使用 `lib/openharmony/<arch>/` 与 `shared/openharmony/<arch>/` 约定
- [ ] 如上游代码需加鸿蒙分支，已按 §4.G fork-patch 纪律处理

### C. 调整压缩算法 / CMake 选项

- [ ] `build_libs/CMakeLists.txt` 的 `option(HDIFFPATCH_USE_*)` 默认值已确认
- [ ] CI 中 Configure 步骤如有 `-DHDIFFPATCH_USE_XXX=` 覆盖，也已同步
- [ ] **构建机与玩家运行时使用同一套压缩算法**，已在 PR 描述中说明兼容策略
- [ ] `docs/build-libs.md` 选项表已更新

### D. Commandlet 子命令 / 参数

- [ ] 参数命名遵循既定语义：`-Old` 老资产 / `-New` 新资产 / `-Out` 输出 / `-Patch` 输入补丁
- [ ] 正确使用 `CheckFileParams` 读取参数（输入类启用 `bCheckExist`）
- [ ] 明确标注所属类别：**生成类**（生产用）或 **自检类**（构建机发版前验证）
- [ ] `docs/commandlet-usage.md` 的参数表与示例已更新

### E. 合并上游 HDiffPatch

- [ ] 仅包含合并上游的改动，**未混入 fork 自身的功能改动**
- [ ] `build_libs/source/HDiffPatch.cpp` 已适配上游接口变化（如有）
- [ ] `build_libs/CMakeLists.txt` 的源文件列表与上游目录一致
- [ ] 已触发 CI，11 个平台全部成功

### F. 文档类 PR

- [ ] 面向人的文档放在 `docs/`，索引写入 `docs/README.md`
- [ ] 面向 AI 的规则放在 `.codebuddy/rules/`，索引写入 `CODEBUDDY.md`
- [ ] 两类文档内容互补、不长段重复

### G. 最小化修改上游（fork-patch） ⚠️

<!-- 仅在第 3 段勾选了 Layer 1 时填写此段 -->

- [ ] 已自检：**无法**在 Layer 2 封装、编译宏、Layer 3 等方式规避
- [ ] 已自检：**不是**上游最新 master 已修复的问题
- [ ] 改动范围最小，未顺手格式化、重命名、重排 include 顺序
- [ ] 代码中用 `// [fork-patch begin] <reason>` 与 `// [fork-patch end]` 包裹
- [ ] 改动已独立 commit，commit message 以 `upstream-patch:` 开头
- [ ] 已在 `docs/upstream-patches.md` 登记表中新增一行
- [ ] 已考虑并说明是否向上游反馈（PR/issue 链接，或"暂不反馈"的原因）

**非改不可的原因**：
<!-- 例如：OpenHarmony 适配必须在 file_for_patch.c 加 __OHOS__ 分支；上游未修复的 bug 链接等 -->


---

## 5. 测试 / 验证

<!-- 说明你是怎么验证这次改动没问题的 -->

- [ ] 本地 `build_libs/` 构建通过（平台：______）
- [ ] UE 插件在本地游戏项目中链接通过（平台：______）
- [ ] Commandlet 本地跑通（命令：______）
- [ ] GitHub CI 已触发并通过（贴 run 链接）：
- [ ] 其他：

## 6. 破坏性变更（Breaking Changes）

- [ ] 本 PR **包含**破坏性变更
- [ ] 本 PR **不含**破坏性变更

> 如包含，请在下方说明影响范围（`HDiffPatch.h` ABI / Commandlet 参数 / 插件对外头）、
> 以及升级建议。

## 7. 禁止提交的内容

请确认 **不包含** 以下任何产物（如误加，请 `git rm --cached` 后重新提交）：

- [ ] `*.obj` / `*.tlog` / `*.pdb` / `*.ilk` / `*.exp`
- [ ] `*.user` / `.vs/` / `.vscode/`（除非项目明确共享）
- [ ] `build/` / `Binaries/` / `Intermediate/` / `DerivedDataCache/`
- [ ] `Saved/` / `.DS_Store` / `Thumbs.db`
- [ ] 不属于本 fork 范围的上游大文件改动（`LICENSE` / `README.md` / `README_cn.md` / `CHANGELOG.md`）

## 8. 相关 Issue / 参考

<!-- 例如：Closes #123，参考上游 commit 链接，设计文档链接等 -->



## 9. 补充说明

<!-- 任何 reviewer 需要知道的背景、取舍、已知问题等 -->


