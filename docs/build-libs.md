# build_libs：跨平台库构建

`build_libs/` 是本 fork 新增的 CMake 工程，作用是把上游 `libHDiffPatch/` 的 C/C++ 源码 +
第三方压缩库源码，编译成 **静态库（archive）** 与 **动态库（shared）**，供 UE 插件 `PPakPatcher` 链接。

## 1. 目录结构

```
build_libs/
├── CMakeLists.txt        # 工程定义
├── CMakePresets.json     # 各平台预设
├── include/
│   └── HDiffPatch.h      # 对外 API（暴露给 UE 项目）
└── source/
    └── HDiffPatch.cpp    # API 实现（隐藏在库中）
```

`include/` 目录下的所有 `.h` 都会作为 PUBLIC 头文件，被 CI 收集并放到
`UEPlugins/PPakPatcher/Source/PPakPatcher/ThirdParty/HDiffPatch/include/`。

## 2. 第三方依赖

CMake 通过 **相对路径** 在仓库同级目录寻找第三方库：

```cmake
ZLIB_DIR  = ${PROJECT_ROOT}/../zlib
BZIP2_DIR = ${PROJECT_ROOT}/../bzip2
LZMA_DIR  = ${PROJECT_ROOT}/../lzma/C
ZSTD_DIR  = ${PROJECT_ROOT}/../zstd/lib
MD5_DIR   = ${PROJECT_ROOT}/../libmd5
XXH_DIR   = ${PROJECT_ROOT}/../xxHash
LDEF_DIR  = ${PROJECT_ROOT}/../libdeflate
```

本地构建前，请先克隆它们到仓库同级目录（与 CI 一致）：

```bash
cd <仓库父目录>
git clone https://github.com/sisong/zlib.git
git clone https://github.com/sisong/zstd.git
git clone https://github.com/sisong/lzma.git
git clone https://github.com/sisong/libdeflate.git
git clone https://github.com/sisong/libmd5.git
git clone https://github.com/sisong/xxHash.git
git clone https://github.com/sisong/bzip2.git    # 仅 Windows 默认开启 BZIP2 时需要
```

## 3. CMake 选项

| 选项 | 默认 | 含义 |
| ---- | ---- | ---- |
| `HDIFFPATCH_USE_ZLIB` | ON | 启用 zlib 压缩 |
| `HDIFFPATCH_USE_BZIP2` | OFF | 启用 bzip2 |
| `HDIFFPATCH_USE_LZMA` | ON | 启用 LZMA / LZMA2 / XZ |
| `HDIFFPATCH_USE_ZSTD` | ON | 启用 zstd |
| `HDIFFPATCH_USE_LDEF` | OFF | 启用 libdeflate |
| `HDIFFPATCH_USE_MD5` | OFF | 启用 MD5 校验 |
| `HDIFFPATCH_USE_XXH` | OFF | 启用 xxHash 校验 |
| `HDIFFPATCH_MT` | ON | 启用多线程（影响 LZMA、ZSTD、libParallel） |

构建时会注入若干内部宏（如 `_IS_NEED_DIR_DIFF_PATCH=1`、`_IS_NEED_BSDIFF=1`、`_IS_NEED_VCDIFF=1`、
`_ChecksumPlugin_fadler64`），保证目录 diff/patch、bsdiff、VCDIFF 兼容能力默认开启。

## 4. 产出目标

`CMakeLists.txt` 同时定义两个 target：

- `HDiffPatchStatic`（输出名 `HDiffPatch`，静态库 `.lib` / `.a`）
- `HDiffPatchShared`（输出名 `HDiffPatch`，动态库 `.dll` / `.so` / `.dylib` / `.framework`）

输出目录：

```
${CMAKE_BINARY_DIR}/static/   # 静态库
${CMAKE_BINARY_DIR}/shared/   # 动态库
```

> iOS 上的动态库会被打包成 `HDiffPatch.framework`，关闭代码签名以方便 CI 直接产出。

## 5. 平台宏与导出宏

CMake 根据目标平台注入：

- `HDIFFPATCH_PLATFORM_WINDOWS` / `_ANDROID` / `_IOS` / `_LINUX` / `_MACOS`
- `HDIFFPATCH_STATIC_LIB=1` (Static target) / `=0` (Shared target)
- `HDIFFPATCH_EXPORTS`（构建库时定义，UE 端引用时不会定义）

`include/HDiffPatch.h` 据此自动选择 `__declspec(dllimport/dllexport)` 或
`__attribute__((visibility("default")))`，最终通过 `HDIFFPATCH_API` 宏修饰对外函数。

## 6. 本地构建示例

### Windows x64

```powershell
cd build_libs
cmake -B ../build/windows-x64 -S . --preset=windows-x64
cmake --build ../build/windows-x64 --config Release
```

### Linux x64

```bash
cd build_libs
cmake -B ../build/linux-x64 -S . --preset=linux-x64
cmake --build ../build/linux-x64 --config Release
```

### Android arm64

```bash
cd build_libs
cmake -B ../build/android-arm64 -S . \
  -DCMAKE_ANDROID_NDK="$NDK_ROOT" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK_ROOT/build/cmake/android.toolchain.cmake" \
  --preset=android-arm64
cmake --build ../build/android-arm64 --config Release
```

### OpenHarmony arm64 / x86_64

需先准备 OpenHarmony NDK 或 OpenHarmony NDK 任一：

| 来源 | 安装方式 |
| ---- | -------- |
| **DevEco Studio**（Win/macOS 推荐） | 安装 DevEco Studio 6+，SDK 自动位于 `C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\native`（Windows） |
| **OpenHarmony NDK**（Linux / 离线推荐） | 从 https://github.com/openharmony-rs/ohos-sdk/releases 下载并解压 |

将环境变量指向 `native/` 那一层：

```powershell
# Windows（DevEco Studio 6.0.0.868 默认）
$env:OHOS_NDK_HOME = "C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\native"
```

```bash
# Linux / 离线 NDK
export OHOS_NDK_HOME=/path/to/ohos-sdk/linux/native
```

构建：

```bash
cd build_libs
cmake -B ../build/openharmony-arm64 -S . --preset=openharmony-arm64
cmake --build ../build/openharmony-arm64 --config Release
```

> 完整 13 个平台的预设见 `CMakePresets.json`，与 CI matrix 一一对应。

## 6.1 用本地 Python 脚本一键构建（推荐）

仓库提供了 `local_build_lib/` 目录，每平台一个脚本，自动处理依赖克隆、cmake 调用、产物归档：

```powershell
# Windows
python local_build_lib\local_build_windows.py
python local_build_lib\local_build_openharmony.py    # 自动探测 DevEco Studio SDK
python local_build_lib\local_build_android.py
```

```bash
# Linux / macOS
python local_build_lib/local_build_linux.py
python local_build_lib/local_build_openharmony.py --ndk-home /path/to/ohos-sdk/.../native
python local_build_lib/build_all.py                 # 一键跑当前 OS 上能跑的所有平台
```

详细说明见 [`local_build_lib/README.md`](../local_build_lib/README.md)。

## 7. 新增对外 API 的步骤

1. 在 `build_libs/include/HDiffPatch.h` 中新增声明，使用 `HDIFFPATCH_API` 宏修饰。
2. 在 `build_libs/source/HDiffPatch.cpp` 中实现，调用 `libHDiffPatch/` 内的算法函数。
3. 同步更新 `docs/api-reference.md`。
4. 同步在 UE 端（`UEPlugins/PPakPatcher/Source/PPakPatcher/Public/` 或 `Private/`）封装为
   UE 友好的接口（`FString`、`TArray<uint8>` 等），避免业务代码直接接触 `unsigned char*`。

## 8. 进一步阅读

- API 列表：[`api-reference.md`](./api-reference.md)
- CI 怎么把库收集到插件里：[`ci-pipeline.md`](./ci-pipeline.md)
