# 在构建机上用 Commandlet 生成补丁

> 关键文件：`UEPlugins/PPakPatcher/Source/PPakPatcherEditor/Private/PPakPatcherCommandlet.cpp`
> 类名：`UPPakPatcherCommandlet`

## 1. 用途

构建机（CI / 发版流水线）拿到两个版本的资产 Pak 后，调用本 Commandlet：

- 生成补丁（`-CreatePakPatch` / `-CreatePakPatchWithDir`）；
- 在构建机上**自检**补丁正确性（`-CheckPakPatch`、`-PatchPak`），确保下发给玩家前补丁是可用的。

补丁生成后，随版本下发给玩家客户端，由 Runtime 模块应用。

## 2. 命令行形式

UE Commandlet 的标准启动方式：

```bash
<UE>/Engine/Binaries/<Platform>/UnrealEditor-Cmd <YourGame>.uproject \
  -run=PPakPatcher \
  -<SubCommand> \
  [-Old=<...>] [-New=<...>] [-Out=<...>] [-Patch=<...>] \
  -unattended -nopause -nullrhi -stdout
```

> Commandlet 注册名一般取自类名去掉前缀 `U` 与后缀 `Commandlet`，即 `PPakPatcher`。
> 若未注册成功，可直接使用 `-run=PPakPatcherCommandlet`。

## 3. 子命令一览

`UPPakPatcherCommandlet::Main` 通过 `FParse::Param` 识别以下开关：

| 子命令 | 角色 | 典型参数 |
| ------ | ---- | -------- |
| `-CreatePakPatch` | 由旧 Pak + 新 Pak 生成补丁 | `-Old` `-New` `-Out` |
| `-CheckPakPatch` | **构建机自检**：校验补丁能否从旧 Pak 正确还原出新 Pak | `-Old` `-New` `-Patch` |
| `-PatchPak` | **构建机自检**：使用补丁 + 旧 Pak 还原出新 Pak，和真实新 Pak 对比 | `-Old` `-Patch` `-Out` |
| `-CreatePakPatchWithDir` | 基于目录而非单一 Pak，批量生成补丁 | 见 §5 |

> 注意：`CheckPakPatch` 与 `PatchPak` 仅用于构建机发版前的自检，不会在玩家设备上运行。
> 玩家设备的 patch 走 Runtime 模块（见 [`runtime-patching.md`](./runtime-patching.md)）。

## 4. 参数约定

所有参数使用 UE 风格 `-Key=Value`：

| 参数 | 含义 | 出现在哪些子命令 |
| ---- | ---- | ---------------- |
| `-Old=<path>` | **老资产**路径（diff 的旧版输入 / patch 的被打补丁对象） | CreatePakPatch, CheckPakPatch, PatchPak |
| `-New=<path>` | **新资产**路径（diff 的新版输入 / 校验时的期望值） | CreatePakPatch, CheckPakPatch |
| `-Out=<path>` | **输出**路径（CreatePakPatch 输出补丁 / PatchPak 输出还原后的新 Pak） | CreatePakPatch, PatchPak |
| `-Patch=<path>` | **输入的补丁**文件路径 | CheckPakPatch, PatchPak |

参数读取由 `CheckFileParams(Params, Match, OutValue, bCheckExist)` 统一处理：

- `FParse::Value(Params, Match, OutValue)` 取值；
- 找不到时打 Error 日志并返回 false；
- 如 `bCheckExist=true`，进一步校验文件是否存在（输入类参数通常开启，输出类通常关闭）。

## 5. 使用示例

### 5.1 生成补丁

```bash
UnrealEditor-Cmd MyGame.uproject -run=PPakPatcher \
  -CreatePakPatch \
  -Old=D:/build/v1/Content.pak \
  -New=D:/build/v2/Content.pak \
  -Out=D:/build/patch/v1_to_v2.hdiff \
  -unattended -nopause -nullrhi -stdout
```

### 5.2 构建机自检 —— 校验补丁正确性

```bash
UnrealEditor-Cmd MyGame.uproject -run=PPakPatcher \
  -CheckPakPatch \
  -Old=D:/build/v1/Content.pak \
  -New=D:/build/v2/Content.pak \
  -Patch=D:/build/patch/v1_to_v2.hdiff \
  -unattended -nopause -nullrhi -stdout
```

### 5.3 构建机自检 —— 用补丁还原新 Pak

```bash
UnrealEditor-Cmd MyGame.uproject -run=PPakPatcher \
  -PatchPak \
  -Old=D:/build/v1/Content.pak \
  -Patch=D:/build/patch/v1_to_v2.hdiff \
  -Out=D:/build/verify/v2_restored.pak \
  -unattended -nopause -nullrhi -stdout
```

还原完成后，可额外用 `fc` / `diff` / `sha256` 将 `v2_restored.pak` 与真实 `v2/Content.pak` 对比。

### 5.4 目录批量生成

```bash
UnrealEditor-Cmd MyGame.uproject -run=PPakPatcher \
  -CreatePakPatchWithDir \
  -Old=D:/build/v1/                    \
  -New=D:/build/v2/                    \
  -Out=D:/build/patch/v1_to_v2_dir/    \
  -unattended -nopause -nullrhi -stdout
```

> `CreatePakPatchWithDir` 的具体目录约定以 `PPakPatcherCommandlet.cpp` 中的实现为准。

## 6. 退出码

- `0` —— 成功（日志 `UPPakPatcherCommandlet - Successed.`）
- 非 `0` —— 失败（日志 `UPPakPatcherCommandlet - Failed. ErrorCode: <code>`）

CI 流水线应据此判断是否中止发版。

## 7. 在 GitHub Actions / Jenkins 中调用的示例

```yaml
- name: Generate & Verify Pak Patch
  shell: bash
  run: |
    UE_CMD="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
    PROJECT="$GAME_PROJECT/MyGame.uproject"
    COMMON_FLAGS="-unattended -nopause -nullrhi -stdout"

    # 1) 生成补丁
    "$UE_CMD" "$PROJECT" -run=PPakPatcher \
      -CreatePakPatch \
      -Old="$BUILD_DIR/v1/Content.pak" \
      -New="$BUILD_DIR/v2/Content.pak" \
      -Out="$BUILD_DIR/patch/v1_to_v2.hdiff" \
      $COMMON_FLAGS

    # 2) 自检：校验补丁
    "$UE_CMD" "$PROJECT" -run=PPakPatcher \
      -CheckPakPatch \
      -Old="$BUILD_DIR/v1/Content.pak" \
      -New="$BUILD_DIR/v2/Content.pak" \
      -Patch="$BUILD_DIR/patch/v1_to_v2.hdiff" \
      $COMMON_FLAGS
```

`-unattended -nopause -nullrhi -stdout` 是构建机跑 Commandlet 的标准抑制交互参数，
缺一会导致进程等待或尝试初始化显示设备而失败。

## 8. 调试

- 日志类目：`LogPakPatcherCommandlet`。
- 也可从编辑器 **File → Run Commandlet** 快速触发（用于本地验证），但生产发版必须走命令行。
- 如需更详细日志，可追加 `-LogCmds="LogPakPatcherCommandlet Verbose"`。

## 9. 进一步阅读

- 运行时应用补丁：[`runtime-patching.md`](./runtime-patching.md)
- 内部使用的底层 API：[`api-reference.md`](./api-reference.md)
