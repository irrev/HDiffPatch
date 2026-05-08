# PPakPatcher 文档中心（人工文档）

本目录存放面向 **人类读者**（开发者、UE 项目集成者、构建机维护者）的项目文档。
AI 编码助手专用的上下文规则请见仓库根目录的 `CODEBUDDY.md` 与 `.codebuddy/rules/`。

---

## 项目定位

本仓库 fork 自 [sisong/HDiffPatch](https://github.com/sisong/HDiffPatch)。
**改造目的**：将 HDiffPatch 二次封装，产出一个跨平台的 UE 引擎插件 **PPakPatcher**，用于：

1. 在游戏构建机上，通过 UE Commandlet 对两个版本的资产（Pak 文件等）生成补丁；
2. 在游戏运行时，使用补丁对老资产打补丁，生成新资产。

完整背景见 [`overview.md`](./overview.md)。

---

## 目录

| 文档 | 说明 |
| ---- | ---- |
| [overview.md](./overview.md) | 项目背景、目标与整体数据流 |
| [architecture.md](./architecture.md) | 仓库结构、上游与本仓的差异、三层架构 |
| [build-libs.md](./build-libs.md) | 如何用 `build_libs/` 生成各平台 HDiffPatch 动态/静态库 |
| [ci-pipeline.md](./ci-pipeline.md) | `ci-build-ueplugins.yml` 工作流详解与产物结构 |
| [ueplugin-usage.md](./ueplugin-usage.md) | UE 项目中如何接入 PPakPatcher 插件 |
| [commandlet-usage.md](./commandlet-usage.md) | 在构建机上调用 `PPakPatcherCommandlet` 生成补丁 |
| [runtime-patching.md](./runtime-patching.md) | 游戏运行时打补丁的接口与流程 |
| [api-reference.md](./api-reference.md) | `build_libs/include/HDiffPatch.h` 暴露的 C++ API |
| [upstream-patches.md](./upstream-patches.md) | 对上游 HDiffPatch 代码的最小化修改登记表 |
| [faq.md](./faq.md) | 常见问题与排错 |

---

## 与根目录文档的关系

- `README.md` / `README_cn.md`：上游 HDiffPatch 项目自带，介绍 HDiffPatch 本体的能力与命令行用法（`hdiffz` / `hpatchz`）。本仓保留以便追踪上游。
- `CHANGELOG.md`：上游 HDiffPatch 的版本变更历史。
- `docs/`（本目录）：**本 fork 新增内容（PPakPatcher 与多平台库构建）** 的专属文档。

---

## 写作约定

- 所有文档使用 Markdown。
- 中英文混排时，中英文之间留一个空格。
- 命令、文件名、函数名一律用反引号包裹。
- 涉及路径时，以仓库根目录为基准的相对路径。
