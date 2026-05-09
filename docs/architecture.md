# 仓库结构与三层架构

## 1. 仓库结构（按职责分组）

```
HDiffPatch/                                        ← 仓库根目录
│
├── [上游 HDiffPatch 原有内容 —— 不修改或谨慎修改]
│   ├── libHDiffPatch/        # 核心 diff/patch 算法
│   ├── libhsync/             # 同步算法（本 fork 暂未使用）
│   ├── libParallel/          # 并行支持
│   ├── dirDiffPatch/         # 目录级 diff/patch
│   ├── bsdiff_wrapper/       # bsdiff 兼容
│   ├── vcdiff_wrapper/       # VCDIFF 兼容
│   ├── hdiffz.cpp / hpatchz.c # 上游 CLI 入口（fork 中不使用）
│   ├── README.md / README_cn.md / CHANGELOG.md
│   └── ...
│
├── [本 fork 新增 —— 库构建层]
│   └── build_libs/
│       ├── CMakeLists.txt        # 跨平台 CMake 工程
│       ├── CMakePresets.json     # 各平台预设 (windows-x64, android-arm64, ios, ...)
│       ├── include/HDiffPatch.h  # 对外 API（暴露给 UE 项目）
│       └── source/HDiffPatch.cpp # API 实现（隐藏在库中）
│
├── [本 fork 新增 —— UE 插件层]
│   └── UEPlugins/PPakPatcher/
│       ├── PPakPatcher.uplugin
│       ├── Resources/
│       └── Source/
│           ├── PPakPatcher/             # Runtime 模块（打包进游戏）
│           │   ├── PPakPatcher.Build.cs
│           │   ├── Public/              # 对外接口头文件
│           │   ├── Private/             # 实现
│           │   └── ThirdParty/HDiffPatch/  # CI 阶段填充：各平台预编译库 + 头文件
│           │       ├── include/
│           │       ├── lib/<platform>/<arch>/    # 静态库
│           │       └── shared/<platform>/<arch>/ # 动态库
│           └── PPakPatcherEditor/       # Editor 模块（仅编辑器/构建机使用）
│               ├── PPakPatcherEditor.Build.cs
│               ├── Public/
│               └── Private/
│                   └── PPakPatcherCommandlet.cpp  # 关键：构建机调用入口
│
└── [本 fork 新增 —— CI]
    └── .github/workflows/
        └── ci-build-ueplugins.yml       # 11 平台并行构建 + 打包发布
```

## 2. 三层架构

```
┌─────────────────────────────────────────────────────────────┐
│  Layer 3 : UE 插件层（PPakPatcher）                         │
│  位置: UEPlugins/PPakPatcher/                               │
│                                                             │
│  ┌─────────────────────┐    ┌──────────────────────────┐   │
│  │  Runtime 模块        │    │  Editor 模块             │   │
│  │  PPakPatcher        │    │  PPakPatcherEditor       │   │
│  │  - 游戏运行时 patch  │    │  - Commandlet 入口        │   │
│  │  - 打包进客户端      │    │  - 仅编辑器/构建机加载    │   │
│  └─────────┬───────────┘    └────────────┬─────────────┘   │
│            │                              │                 │
│            └────────────┬─────────────────┘                 │
│                         │ 调用                              │
└─────────────────────────┼───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer 2 : 跨平台库层（HDiffPatch.dll/.so/.a/.framework）   │
│  位置: build_libs/                                          │
│                                                             │
│  - include/HDiffPatch.h  : C++ namespace HDiffPatch         │
│      CreateDiff / Patch / CreateSingleCompressedDiff /      │
│      PatchSingleStream / GetSingleCompressedDiffInfo / ...  │
│  - source/HDiffPatch.cpp : 实现，调用 Layer 1                │
│  - 通过 CMake + Presets 为每个目标平台产出独立库文件         │
└─────────────────────────┬───────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  Layer 1 : 上游 HDiffPatch 核心算法（保持不变）             │
│  位置: libHDiffPatch/ + libParallel/ + 第三方压缩库          │
│                                                             │
│  - libHDiffPatch/HDiff   : diff 算法                         │
│  - libHDiffPatch/HPatch  : patch 算法                        │
│  - 第三方依赖（克隆到仓库同级目录）：                         │
│      ../zlib  ../zstd  ../lzma  ../libdeflate                │
│      ../bzip2 ../libmd5 ../xxHash                            │
└─────────────────────────────────────────────────────────────┘
```

### 各层的修改原则

| 层 | 修改频率 | 谁负责 | 原则 |
| ---- | ---- | ---- | ---- |
| Layer 1 (上游) | 最小化修改 | 上游维护者为主；本 fork 仅在必要时打 `fork-patch` | 默认不改；遇 bug / 平台适配 / 安全问题等必要性可最小化修改，见 [`upstream-patches.md`](./upstream-patches.md) |
| Layer 2 (build_libs) | 当需要新增对外 API、增减压缩算法、新增目标平台时修改 | 本 fork | 不得依赖 UE 类型 |
| Layer 3 (UEPlugins) | UE 端业务逻辑变化时修改 | 本 fork | 遵循 UE 规范 |

## 3. 构建产物的最终归宿

CI 完成后，所有平台的库会被复制到：

```
UEPlugins/PPakPatcher/Source/PPakPatcher/ThirdParty/HDiffPatch/
├── include/HDiffPatch.h
├── lib/
│   ├── windows/{x64,x86}/HDiffPatch.lib
│   ├── android/{arm64,armeabi,x86,x86_64}/libHDiffPatch.a
│   ├── linux/{arm,x86,x64}/libHDiffPatch.a
│   ├── macos/libHDiffPatch.a
│   ├── ios/libHDiffPatch.a
│   └── harmonyos/{arm64,x86_64}/libHDiffPatch.a
└── shared/
    ├── windows/{x64,x86}/HDiffPatch.dll
    ├── android/{arm64,armeabi,x86,x86_64}/libHDiffPatch.so
    ├── linux/{arm,x86,x64}/libHDiffPatch.so
    ├── macos/libHDiffPatch.dylib
    ├── ios/HDiffPatch.framework
    └── harmonyos/{arm64,x86_64}/libHDiffPatch.so
```

> Layer 3（UE 插件）目前在 `PPakPatcher.Build.cs` 中尚未加入 HarmonyOS 分支，
> 等待 UE 官方支持鸿蒙 Target 后再补；阶段 1 仅产出库供后续接入。

`PPakPatcher.Build.cs` 据此按平台选择正确的库链接。

## 4. 进一步阅读

- 库构建：[`build-libs.md`](./build-libs.md)
- CI 流程：[`ci-pipeline.md`](./ci-pipeline.md)
