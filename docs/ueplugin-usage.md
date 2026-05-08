# 在 UE 项目中使用 PPakPatcher

## 1. 获取插件

两种方式：

### 方式 A：使用 CI 产出的 release zip（推荐）

1. 在 GitHub Actions 手动触发 `CMake UEPlugins-PakPatcher` 工作流；
2. 等待完成后，从 release 下载 `UEPlugins-PakPatcher.zip`；
3. 解压后将整个 `PPakPatcher/` 目录拷贝到 UE 项目的 `Plugins/` 下：

   ```
   <YourGameProject>/
   └── Plugins/
       └── PPakPatcher/
           ├── PPakPatcher.uplugin
           ├── Resources/
           └── Source/
               ├── PPakPatcher/
               │   ├── ThirdParty/HDiffPatch/   ← 已包含各平台预编译库
               │   └── ...
               └── PPakPatcherEditor/
   ```

### 方式 B：本地手工构建

1. 按 [`build-libs.md`](./build-libs.md) 编译当前需要的目标平台。
2. 手动把库与头文件按 `ci-pipeline.md` 中的约定路径放到
   `UEPlugins/PPakPatcher/Source/PPakPatcher/ThirdParty/HDiffPatch/`。
3. 拷贝整个 `UEPlugins/PPakPatcher/` 到游戏项目的 `Plugins/`。

## 2. 启用插件

打开游戏项目，在 `<Project>.uproject` 中加入：

```json
{
  "Plugins": [
    { "Name": "PPakPatcher", "Enabled": true }
  ]
}
```

或在 UE 编辑器 **Edit → Plugins** 里手动启用。

## 3. 插件模块构成

`PPakPatcher.uplugin` 定义了两个模块：

| 模块 | Type | LoadingPhase | 用途 |
| ---- | ---- | ------------ | ---- |
| `PPakPatcher` | Runtime | PostConfigInit | 运行时打补丁（打包进客户端） |
| `PPakPatcherEditor` | Editor | Default | 提供 Commandlet（仅编辑器/构建机加载） |

> Runtime 模块在 PostConfigInit 阶段加载，可以在很早期被 GameInstance、Bootstrapper 调用。

## 4. 在 C++ 代码中引用

在你模块的 `*.Build.cs` 中添加依赖：

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "PPakPatcher",      // Runtime 模块
});
```

如果是 Editor 端代码（如自动化、自定义工具），也加：

```csharp
if (Target.bBuildEditor)
{
    PrivateDependencyModuleNames.Add("PPakPatcherEditor");
}
```

然后在 `.cpp` 中：

```cpp
#include "PPakPatcherModule.h"
// 或 PPakPatcher 模块对外暴露的具体头文件（位于 Source/PPakPatcher/Public/）
```

## 5. 平台库的链接

`PPakPatcher.Build.cs` 负责按 `Target.Platform` / `Target.Architecture` 选择
`ThirdParty/HDiffPatch/lib/<platform>/<arch>/` 下对应的库进行链接。

如果链接失败，常见原因：

- 当前 UE 目标架构对应的库未在 CI 中产出（请查 matrix）；
- ThirdParty 目录被误删，未被 CI 重新填充；
- `PPakPatcher.Build.cs` 的平台分支与 CI 拷贝路径不一致。

## 6. 进一步阅读

- 构建机调用 Commandlet：[`commandlet-usage.md`](./commandlet-usage.md)
- 运行时打补丁：[`runtime-patching.md`](./runtime-patching.md)
- 对外 C++ API：[`api-reference.md`](./api-reference.md)
