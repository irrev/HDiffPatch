# 上游改动登记表（Upstream Patches）

本仓 fork 自 [sisong/HDiffPatch](https://github.com/sisong/HDiffPatch)，原则上 **Layer 1 上游代码保持不变**。
但在遇到 bug、平台适配、安全问题、第三方 API 变动等必要性场景时，允许进行**最小化修改**。

**本文件用于登记所有对上游代码的修改**，便于：

- 跟踪每一处 fork 专属改动的原因；
- 将来 `git merge upstream/master` 时快速识别冲突点；
- 判断改动是否应该反馈给上游，或已被上游修复后可回退。

---

## 修改规则（摘要）

完整规则见 `.codebuddy/rules/ai-workflow.md` 任务 I。关键点：

1. **先规避，后修改**：能在 Layer 2 / Layer 3 / CMake 宏里解决的，不动 Layer 1。
2. **范围最小**：只改必要的行，不顺手重构。
3. **打标记**：代码中用 `// [fork-patch begin] <reason>` 与 `// [fork-patch end]` 包裹。
4. **独立 commit**：commit message 用 `upstream-patch:` 前缀。
5. **登记到本表**：每次修改新增一行。
6. **PR 声明**：在 PR 模板里勾选"影响 Layer 1"。

---

## 登记表

> 字段说明：
> - **文件:行号**：改动位置。如跨多行，记起止行或"整函数名"。
> - **原因**：一句话说明为什么非改不可。
> - **Commit**：独立 commit 的 hash（短格式）或 PR 链接。
> - **上游状态**：`已反馈`（PR/issue 链接）/ `未反馈` / `上游已修复`（对应上游 commit / 版本）。
> - **可回退**：若上游已修复，是否可在下次 merge 后移除本 patch。

| 日期 | 文件:行号 | 原因 | Commit | 上游状态 | 可回退 |
| ---- | --------- | ---- | ------ | -------- | ------ |
| _(当前无登记)_ | | | | | |

<!--
示例行（真正登记时取消注释并填写）：

| 2026-05-15 | libHDiffPatch/HPatch/patch.c:L123-L130 | HarmonyOS 缺少 POSIX 某 API，加 `__OHOS__` 分支 | abc1234 | 未反馈 | 否 |
| 2026-05-20 | file_for_patch.c:fopen_wrapper | Windows 上长路径处理 bug | def5678 | [上游 PR #999](https://github.com/sisong/HDiffPatch/pull/999) | 上游合并后可回退 |
-->

---

## 操作手册

### A. 新增一处 fork-patch

1. 按任务 I 的流程修改代码，加 `// [fork-patch begin] ... // [fork-patch end]` 标记。
2. 单独 commit：`git commit -m "upstream-patch: <简述>"`。
3. 在本表登记一行。
4. PR 模板勾选"影响 Layer 1"并链接到本表的对应行。

### B. Merge 上游后的维护

1. 执行 `git fetch upstream && git merge upstream/master`。
2. 若冲突在带 `// [fork-patch]` 标记的代码附近，**优先保留 fork-patch 并确认新上游代码未修复同一问题**。
3. 若发现上游已修复（提交来自上游的官方修复），把本表该行的"上游状态"更新为 `上游已修复`，
   "可回退"标记为 `是`，并在下一次独立 commit 中移除 `[fork-patch]` 代码段。
4. 合并上游**不要**与移除 fork-patch 混在同一个 commit，保持审计清晰。

### C. 排查 "哪些文件被改过"

快速列出当前所有 fork-patch 标记：

```bash
# 代码中的标记
git grep -n "\[fork-patch" -- libHDiffPatch libhsync libParallel dirDiffPatch \
    bsdiff_wrapper vcdiff_wrapper file_for_patch.* hdiffz.cpp hpatchz.c

# 提交历史中的上游改动
git log --oneline --grep="^upstream-patch:"
```

两者应当与本登记表一一对应。

---

## 参考

- 修改剧本：`.codebuddy/rules/ai-workflow.md` 任务 I
- 上游仓库：https://github.com/sisong/HDiffPatch
