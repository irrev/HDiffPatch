# 模块地图（AI 上下文）

## 1. 总览

```
HDiffPatch/
│
├── [Layer 1 — 上游核心，保持不变]
│   ├── libHDiffPatch/              # diff / patch 算法
│   │   ├── HDiff/                  #   - diff 端
│   │   └── HPatch/                 #   - patch 端
│   ├── libhsync/                   # 同步算法（本 fork 未使用）
│   ├── libParallel/                # 线程池 / 并行
│   ├── dirDiffPatch/               # 目录级 diff/patch
│   ├── bsdiff_wrapper/             # bsdiff 兼容
│   ├── vcdiff_wrapper/             # VCDIFF 兼容
│   ├── hdiffz.cpp / hpatchz.c      # 上游 CLI（本 fork 不产出 CLI）
│   ├── file_for_patch.*            # 文件 IO
│   └── _atosize.h / _hextobytes.h / _clock_for_demo.h / _dir_ignore.h
│
├── [Layer 2 — 跨平台库层，本 fork 新增]
│   └── build_libs/
│       ├── CMakeLists.txt          # 会 glob 引入 Layer 1 源文件一起编译
│       ├── CMakePresets.json       # 11 个平台预设（与 CI matrix 对应）
│       ├── include/HDiffPatch.h    # 对外 C++ API，namespace HDiffPatch
│       └── source/HDiffPatch.cpp   # 封装 Layer 1 的实现
│
├── [Layer 3 — UE 插件层，本 fork 新增]
│   └── UEPlugins/PPakPatcher/
│       ├── PPakPatcher.uplugin     # Runtime + Editor 双模块
│       ├── Resources/              # 图标等
│       └── Source/
│           ├── PPakPatcher/                       # Runtime 模块
│           │   ├── PPakPatcher.Build.cs
│           │   ├── Public/                        # 14 个 .h 对外头
│           │   ├── Private/                       # 16 个 .cpp/.h 实现
│           │   └── ThirdParty/HDiffPatch/         # CI 填充：预编译库 + 头
│           │       ├── include/
│           │       ├── lib/<platform>/<arch>/
│           │       └── shared/<platform>/<arch>/
│           └── PPakPatcherEditor/                 # Editor 模块
│               ├── PPakPatcherEditor.Build.cs
│               ├── Public/
│               └── Private/
│                   └── PPakPatcherCommandlet.cpp  # 构建机入口
│
├── [CI]
│   └── .github/workflows/ci-build-ueplugins.yml   # 11 平台并行 + release 拼包
│
├── [文档]
│   ├── README.md / README_cn.md / CHANGELOG.md   # 上游
│   ├── docs/                                      # 人工文档（本 fork）
│   ├── CODEBUDDY.md                               # AI 主入口
│   └── .codebuddy/rules/                          # AI 细则
│
└── [其他]
    ├── builds/                     # 上游各平台 IDE 工程
    ├── build_libs/                 # (见 Layer 2)
    ├── testhdiffpatch/             # 上游测试工程（含构建产物，别动）
    └── test/                       # 上游单元测试
```

## 2. 依赖方向（严格自上而下）

```
UE 游戏项目
    │
    ▼
PPakPatcherEditor      (仅编辑器/构建机加载)
    │ 引用
    ▼
PPakPatcher (Runtime)  (也打包进客户端)
    │ 通过 ThirdParty/HDiffPatch 链接
    ▼
HDiffPatch 动态/静态库  (build_libs 产物)
    │ 源码来源
    ▼
libHDiffPatch + libParallel + ../zlib ../zstd ../lzma ../libdeflate ../bzip2 ../libmd5 ../xxHash
```

**禁止反向依赖**：

- Layer 2 不能 `#include` 任何 UE 头或 `PPakPatcher*`；
- Layer 1 不能感知 Layer 2、Layer 3 的存在。

## 3. UE 插件的两个模块

### Runtime 模块 `PPakPatcher`

- `Type: Runtime`, `LoadingPhase: PostConfigInit`
- 打包进游戏客户端，**必须在所有目标平台上可用**（Win/Android/iOS/Linux/Mac）。
- 面向玩家设备：做 patch；如启用 `HDIFFPATCH_ENABLE_DIFF=1` 也能做 diff，但客户端一般不需要。

### Editor 模块 `PPakPatcherEditor`

- `Type: Editor`, `LoadingPhase: Default`
- **仅编辑器 / 构建机** 加载，**不会** 打包进发行客户端。
- 主要提供 `UPPakPatcherCommandlet`，供构建机脚本调用。

## 4. 第三方库

第三方依赖通过 `../<libname>` 相对路径引用（与上游约定一致）：

```
<parent>/
├── HDiffPatch/      (本仓)
├── zlib/
├── zstd/
├── lzma/
├── libdeflate/
├── bzip2/
├── libmd5/
└── xxHash/
```

CI 中通过 `git clone https://github.com/sisong/<lib>.git ../<lib>` 准备。
AI 修改 `build_libs/CMakeLists.txt` 时 **不要把路径改成绝对路径或换源**。

## 5. Commandlet 子命令与参数（事实依据）

`UPPakPatcherCommandlet::Main` 解析以下开关（通过 `FParse::Param`），用途分两类：

**生成类**（生产用途）：

| 开关 | 行为 | 参数 |
| ---- | ---- | ---- |
| `-CreatePakPatch` | 由旧 Pak + 新 Pak 生成补丁 | `-Old` `-New` `-Out` |
| `-CreatePakPatchWithDir` | 基于目录批量生成补丁 | `-Old` `-New` `-Out`（目录） |

**自检类**（构建机发版前验证补丁可用性）：

| 开关 | 行为 | 参数 |
| ---- | ---- | ---- |
| `-CheckPakPatch` | 校验"旧 Pak + 补丁 == 新 Pak" | `-Old` `-New` `-Patch` |
| `-PatchPak` | 用补丁还原新 Pak（可与真实新 Pak 对比） | `-Old` `-Patch` `-Out` |

**参数语义**（统一约定）：

- `-Old=<path>` ：**老**资产
- `-New=<path>` ：**新**资产
- `-Out=<path>` ：**输出**（补丁文件或还原后的 Pak）
- `-Patch=<path>` ：**输入**的补丁文件

> 注意：当前源码中 `-CheckPakPatch` / `-PatchPak` 的实现分支仍然调用了 `CreatePakPatch`
> （占位代码）。两个子命令的语义已确定（见上表），如需真正实现校验与还原，
> 分别对应 Layer 2 的 `CheckSingleCompressedDiff` / `PatchSingleStream`（参见 `build_libs/include/HDiffPatch.h`）。
> AI 发现此占位实现时，可在用户确认后按上表语义补齐。
