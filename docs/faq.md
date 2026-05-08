# FAQ

## Q1. 我只想用插件，需要自己跑 CI 吗？

不需要。手动触发 GitHub Actions 中的 `CMake UEPlugins-PakPatcher` 工作流，
等它跑完后从 release 下载 `UEPlugins-PakPatcher.zip`，解压拖到 UE 项目 `Plugins/` 下即可。

## Q2. 本地构建 `build_libs/` 时报找不到 `zlib.h` / `zstd.h`？

第三方库需克隆到仓库 **同级目录**（不是仓库内）。详见 [`build-libs.md`](./build-libs.md) §2。

## Q3. CI 上某个平台失败，我能否只重跑这个平台？

可以。Actions 页面点 "Re-run failed jobs" 即可，`fail-fast: false` 已经保证一个平台失败不会拖累其他平台。

## Q4. 如何新增一个目标平台 / 架构？

四处同步修改：

1. `build_libs/CMakePresets.json` 增加预设；
2. `.github/workflows/ci-build-ueplugins.yml` 的 matrix 与 release job `copy_lib`；
3. `UEPlugins/PPakPatcher/Source/PPakPatcher/PPakPatcher.Build.cs` 的平台分支；
4. `docs/architecture.md`、`docs/ci-pipeline.md` 的产物表更新。

## Q5. 想要更换/裁剪压缩算法？

修改 `build_libs/CMakeLists.txt` 顶部 `option(HDIFFPATCH_USE_*)` 默认值，或在 CI Configure 步骤
通过 `-DHDIFFPATCH_USE_XXX=OFF` 覆盖。注意必须和补丁生成端一致：构建机用什么压缩算法生成补丁，
玩家端的库就必须启用同一个算法。

## Q6. UE 链接时报找不到 `HDiffPatch.lib` / `libHDiffPatch.so`？

按以下顺序排查：

1. CI release zip 解压时是否完整？检查 `ThirdParty/HDiffPatch/lib/<platform>/<arch>/`。
2. UE Target 平台/架构是否在 CI matrix 中？不在的话需要补一份。
3. `PPakPatcher.Build.cs` 里挑选库的分支是否覆盖了当前平台？

## Q7. Commandlet 在编辑器里能跑，构建机上跑不起来？

构建机一般用 `UnrealEditor-Cmd`（或老引擎的 `UE4Editor-Cmd`），并加上：

```
-unattended -nopause -nullrhi -stdout
```

否则 Commandlet 会因为找不到显示设备或等待用户交互而挂住。

## Q8. 玩家端 patch 失败，先怀疑什么？

按高频从高到低：

1. 旧 Pak 在玩家设备上被改过 / 损坏（哈希不一致）；
2. 补丁文件下载不完整；
3. 客户端集成的 HDiffPatch 库版本与构建机版本不一致（导致 header 解析不兼容）；
4. 内存不足，特别是低端 Android。

可先调用 `GetSingleCompressedDiffInfo` 拿到补丁 header 信息，确认 `compressType` 和 `newDataSize`
是预期值。

## Q9. 上游 HDiffPatch 升级了，我该如何同步？

```bash
git remote add upstream https://github.com/sisong/HDiffPatch.git
git fetch upstream
git merge upstream/master   # 或 rebase
```

注意冲突主要可能出现在：

- `libHDiffPatch/` 内部接口签名变更 → 需要相应更新 `build_libs/source/HDiffPatch.cpp`；
- 第三方库版本变化 → 检查 `build_libs/CMakeLists.txt` 的源文件列表是否仍然存在。

`build_libs/`、`UEPlugins/`、`.github/workflows/ci-build-ueplugins.yml` 都是本 fork 独有，
理论上不会与上游冲突。
