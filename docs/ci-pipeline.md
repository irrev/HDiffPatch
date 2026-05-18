# CI 流水线：ci-build-ueplugins.yml

文件位置：`.github/workflows/ci-build-ueplugins.yml`
作用：在 GitHub Actions 上一键产出 **可直接拖进 UE 项目的 PPakPatcher 插件 zip 包**。

## 1. 触发方式

```yaml
on:
  workflow_dispatch:        # 仅手动触发
    inputs:
      tag_date:    # 用于拼接 release tag (v1.0.0_<tag_date>)
      overwrite:   # 是否删除已存在的同名 release 后重发
```

不会被 push 或 PR 自动触发，避免每次提交都跑这条重型流水线。

## 2. Job 结构

```
build (matrix: 11 platforms)  ──┐
                                │
                                ├──>  release (ubuntu-latest)
                                │      ├─ 下载所有平台 artifact
                                │      ├─ 按目录约定拷贝到 ThirdParty/HDiffPatch/
                                │      ├─ 拷贝 include/*.h
                                │      ├─ 打 zip → UEPlugins-PakPatcher.zip
                                │      └─ 创建 GitHub Release 并上传
```

## 3. 平台 matrix

| build_target | runner | preset |
| ------------ | ------ | ------ |
| windows-x64 | windows-latest | windows-x64 |
| windows-x86 | windows-latest | windows-x86 |
| android-arm64 | ubuntu-latest | android-arm64 |
| android-armeabi | ubuntu-latest | android-armeabi |
| android-x86 | ubuntu-latest | android-x86 |
| android-x86_64 | ubuntu-latest | android-x86_64 |
| linux-arm | ubuntu-latest | linux-arm |
| linux-x86 | ubuntu-latest | linux-x86 |
| linux-x64 | ubuntu-latest | linux-x64 |
| macos | macos-latest | macos |
| ios | macos-latest | ios |
| openharmony-arm64 | ubuntu-latest | openharmony-arm64 |
| openharmony-x86_64 | ubuntu-latest | openharmony-x86_64 |

`fail-fast: false` 保证某个平台失败时其他平台仍继续，便于一次性看清所有问题。

## 4. build job 关键步骤

1. `actions/checkout@v4` —— 拉取本仓代码。
2. **Checkout Dependencies** —— 把第三方库克隆到仓库 **同级目录**：

   ```bash
   git clone https://github.com/sisong/libmd5.git ../libmd5
   git clone https://github.com/sisong/xxHash.git ../xxHash
   git clone https://github.com/sisong/lzma.git   ../lzma
   git clone https://github.com/sisong/zstd.git   ../zstd
   git clone https://github.com/sisong/zlib.git   ../zlib
   git clone https://github.com/sisong/libdeflate.git ../libdeflate
   # Windows 平台额外:
   git clone https://github.com/sisong/bzip2.git ../bzip2
   ```

3. 安装工具链：CMake + Ninja、Xcode（mac/iOS）、MSVC（windows）、NDK + JDK + Android SDK（android）、OpenHarmony NDK（OpenHarmony，从 GitHub 镜像 `openharmony-rs/ohos-sdk` 下载，受 env `OHOS_SDK_VERSION` 控制）。
4. **Configure CMake** —— 根据平台选用对应分支：
   - Android 额外注入 `-DCMAKE_ANDROID_NDK` 和 `-DCMAKE_TOOLCHAIN_FILE`；
   - OpenHarmony 通过 `OHOS_NDK_HOME` 环境变量解析 preset 中的 toolchain 路径。
5. `cmake --build ... --config Release` —— 出包。
6. `actions/upload-artifact@v4` —— 上传整个 build 目录，artifact 名 = preset 名。

## 5. release job 关键步骤

下载所有 artifact 后，按下方约定的目录结构拷贝库文件，拼装出最终的 UE 插件目录：

```
result/UEPlugins/PPakPatcher/Source/PPakPatcher/ThirdParty/HDiffPatch/
├── include/                              # 来自 build_libs/include/*.h
├── lib/                                  # 静态库
│   ├── windows/{x64,x86}/HDiffPatch.lib
│   ├── android/{arm64,armeabi,x86,x86_64}/libHDiffPatch.a
│   ├── linux/{arm,x86,x64}/libHDiffPatch.a
│   ├── macos/libHDiffPatch.a
│   ├── ios/libHDiffPatch.a
│   └── openharmony/{arm64,x86_64}/libHDiffPatch.a
└── shared/                               # 动态库
    ├── windows/{x64,x86}/HDiffPatch.dll
    ├── android/{arm64,armeabi,x86,x86_64}/libHDiffPatch.so
    ├── linux/{arm,x86,x64}/libHDiffPatch.so
    ├── macos/libHDiffPatch.dylib
    ├── ios/HDiffPatch.framework
    └── openharmony/{arm64,x86_64}/libHDiffPatch.so
```

> macOS 与 iOS 的 artifact 路径多一层 `Release/`，是 Xcode 多配置生成器的产物形态，CI 中已正确处理。

最后：

- 将 `result/UEPlugins/` 打包为 `UEPlugins-PakPatcher.zip`；
- 创建 release tag `v1.0.0_<tag_date>`；
- 上传 zip 作为 release asset。

## 6. 常见维护点

| 改动场景 | 需要同步修改的位置 |
| -------- | ------------------ |
| 新增一个目标平台 | matrix 增加项 + `CMakePresets.json` 新预设 + release job 中的 `copy_lib` 段 + `PPakPatcher.Build.cs` |
| 新增一个第三方依赖 | "Checkout Dependencies" 步骤 + `build_libs/CMakeLists.txt` 新增 option/源文件列表 |
| 升级 NDK / Xcode / CMake | 文件顶部 `env:` 段统一修改 |
| 改变插件目录结构 | `BASE_PATH` 与 `copy_lib` 调用 |

## 7. 进一步阅读

- 库构建本身：[`build-libs.md`](./build-libs.md)
- 插件目录约定：[`architecture.md`](./architecture.md)
- UE 端如何接：[`ueplugin-usage.md`](./ueplugin-usage.md)
