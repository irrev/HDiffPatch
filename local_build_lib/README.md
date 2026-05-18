# 本地构建脚本（local_build_lib）

提供 Python 脚本，在本地一键编译 `build_libs/` 工程，按平台产出 HDiffPatch 静态/动态库，
便于开发自检。**生产发版仍以 GitHub CI（`ci-build-ueplugins.yml`）为准**。

## 1. 文件结构

```
local_build_lib/
├── _common.py                  # 公共逻辑：依赖克隆、cmake configure/build、产物归档
├── local_build_windows.py      # x64 + x86
├── local_build_android.py      # arm64 + armeabi + x86 + x86_64
├── local_build_linux.py        # arm + x86 + x64
├── local_build_macos.py        # universal (arm64 + x86_64)
├── local_build_ios.py          # arm64
├── local_build_openharmony.py    # arm64 + x86_64
├── build_all.py                # 一键跑当前 OS 上能跑的所有平台
├── README.md                   # 本文件
├── build/                      # CMake 临时构建目录（git 忽略）
└── out/                        # 最终归档目录（git 忽略）
    └── <platform>/
        ├── lib/<arch>/         # 静态库
        └── shared/<arch>/      # 动态库
```

## 2. 通用前置

- 安装 Python 3.9+
- 安装 CMake 3.23.5+，确保 `cmake` 在 PATH 中
- 安装 Git，会自动把第三方依赖克隆到 **仓库同级目录**：
  ```
  <parent>/
  ├── HDiffPatch/    （本仓）
  ├── lzma/  ../zstd/  ../zlib/  ../libdeflate/  ../libmd5/  ../xxHash/
  └── （Windows 还会克隆 ../bzip2/）
  ```
- 与 CI 完全一致的依赖来源：`https://github.com/sisong/<lib>.git`

## 3. 各平台用法

### 3.1 Windows（x64 + x86）

**前置**：Visual Studio 2019/2022，需要在 *Developer Command Prompt for VS* 或
*x64/x86 Native Tools Command Prompt* 中运行（保证 `cl.exe` 在 PATH）。

```powershell
python local_build_lib\local_build_windows.py             # 编译 x64+x86
python local_build_lib\local_build_windows.py --archs x64 # 仅 x64
```

### 3.2 Android（arm64 / armeabi / x86 / x86_64）

**前置**：Android NDK r27（与 CI 一致），并设环境变量任一：
`NDK_ROOT` / `NDKROOT` / `ANDROID_NDK_HOME` / `ANDROID_NDK_ROOT`。

```powershell
python local_build_lib\local_build_android.py
python local_build_lib\local_build_android.py --archs arm64,x86_64
```

### 3.3 Linux（arm / x86 / x64）

**前置**：仅 Linux 宿主机，安装 clang。

```bash
python local_build_lib/local_build_linux.py
python local_build_lib/local_build_linux.py --archs x64
```

### 3.4 macOS / iOS

**前置**：仅 macOS 宿主机，安装 Xcode 16+。

```bash
python local_build_lib/local_build_macos.py
python local_build_lib/local_build_ios.py
```

### 3.5 OpenHarmony（arm64 + x86_64）

**前置**：以下任一 SDK 即可，脚本按优先级自动探测。

| 来源 | 路径示例 | 说明 |
| ---- | -------- | ---- |
| **DevEco Studio**（推荐 Win/macOS） | `C:\Program Files\Huawei\DevEco Studio\sdk\default\openharmony\native` | 安装 DevEco Studio 即自带 |
| **OpenHarmony NDK**（推荐 Linux / 离线包）| 解压后 `<...>/native/` | https://github.com/openharmony-rs/ohos-sdk/releases |
| **OpenHarmony NDK**（华为商业版） | DevEco Studio 内 `sdk/default/hms/native` | 脚本会自动 fallback |

**探测顺序**：
1. `--ndk-home <path>` 命令行参数
2. 环境变量 `OHOS_NDK_HOME` / `OHOS_SDK_NATIVE` / `HMOS_NDK_HOME`
3. 环境变量 `DEVECO_PATH` 推导出 `sdk/default/openharmony/native`
4. 各 OS 的默认 DevEco Studio 安装路径

```powershell
python local_build_lib\local_build_openharmony.py
python local_build_lib\local_build_openharmony.py --archs arm64
python local_build_lib\local_build_openharmony.py --ndk-home D:\ohos-sdk\linux\native
```

## 4. 一键构建

```bash
python local_build_lib/build_all.py
```

按宿主机能力自动决定平台集合（Windows: windows+android+OpenHarmony；
macOS 还会带上 macos+ios），任一失败不中断，最终汇总。

## 5. 产物结构

构建完成后：

```
local_build_lib/out/
├── windows/
│   ├── lib/{x64,x86}/HDiffPatch.lib
│   └── shared/{x64,x86}/HDiffPatch.dll
├── android/
│   ├── lib/{arm64,armeabi,x86,x86_64}/libHDiffPatch.a
│   └── shared/{arm64,armeabi,x86,x86_64}/libHDiffPatch.so
├── OpenHarmony/
│   ├── lib/{arm64,x86_64}/libHDiffPatch.a
│   └── shared/{arm64,x86_64}/libHDiffPatch.so
└── ...
```

> 注意：本地脚本输出在 `local_build_lib/out/`，与 CI 输出目录
> （`UEPlugins/PPakPatcher/Source/PPakPatcher/ThirdParty/HDiffPatch/`）**不同**。
> 如需将本地产物复制到 UE 插件第三方目录，请手工或通过额外脚本完成。

## 6. 与 CI 的一致性

本脚本严格复用 `build_libs/CMakePresets.json` 中的 preset 名，与 CI 的 matrix preset 一一对应：

| 本地命令 | preset | CI matrix.preset |
| -------- | ------ | ---------------- |
| `local_build_windows.py --archs x64` | `windows-x64` | `windows-x64` |
| `local_build_android.py --archs arm64` | `android-arm64` | `android-arm64` |
| `local_build_openharmony.py --archs arm64` | `openharmony-arm64` | `openharmony-arm64` |
| ... | ... | ... |

因此**本地通过 ⇒ CI 大概率也通过**（差异主要是 NDK 版本、宿主工具链）。

## 7. 故障排查

| 现象 | 排查 |
| ---- | ---- |
| `cmake: command not found` | 安装 CMake 并加入 PATH |
| `cl.exe not found`（Windows） | 在 *Developer Command Prompt for VS* 中重新运行 |
| Android：`Could not find NDK` | 设置 `NDK_ROOT` 环境变量 |
| OpenHarmony：未找到 toolchain | 启动 DevEco Studio 一次让其下载 SDK；或用 `--ndk-home` 显式指定 |
| 第三方库目录已存在但是错的 | 删除 `<parent>/<libname>/` 让脚本重克隆 |
