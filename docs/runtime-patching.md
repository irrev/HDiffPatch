# 运行时打补丁

游戏在玩家设备上下载到补丁文件后，由 PPakPatcher 的 **Runtime 模块** 完成打补丁，
将旧版资产升级为新版资产。

## 1. 推荐流程

```
[启动 / 资源更新阶段]
  1. 下载补丁文件（patch.hdiff）到本地缓存目录
  2. 定位旧版资产路径（old.pak）
  3. 调用 PPakPatcher Runtime API 进行 patch
  4. patch 成功 → 替换旧文件 / 切换 mount
  5. patch 失败 → 回滚或重新走全量下载
```

## 2. 推荐使用的算法接口

底层 `build_libs/include/HDiffPatch.h` 提供两组接口：

### 2.1 单步压缩 patch（推荐）

```cpp
HDiffPatch::PatchSingleStream(
    /*out*/ unsigned char* out_newData, unsigned char* out_newData_end,
    const   unsigned char* oldData,     const unsigned char* oldData_end,
    const   unsigned char* diff,        const unsigned char* diff_end,
    size_t threadNum = 1);
```

补丁由 `CreateSingleCompressedDiff` 在构建机上生成。优点：

- 单流格式、压缩补丁体积更小；
- 可通过 `GetSingleCompressedDiffInfo` 提前读取补丁元信息（`newDataSize`、压缩算法、推荐内存等），
  便于在 patch 前预分配缓冲、显示进度。

### 2.2 经典 diff/patch

```cpp
HDiffPatch::Patch(
    /*out*/ unsigned char* out_newData, unsigned char* out_newData_end,
    const   unsigned char* oldData,     const unsigned char* oldData_end,
    const   unsigned char* serializedDiff,
    const   unsigned char* serializedDiff_end);
```

补丁由 `CreateDiff` 生成。兼容上游 HDiffPatch 经典格式。

## 3. UE 端的封装建议

由于上述 API 使用裸指针，直接在游戏代码里调用容易出错。推荐由 PPakPatcher Runtime 模块提供
UE 友好的二次封装，例如：

```cpp
// 位于 UEPlugins/PPakPatcher/Source/PPakPatcher/Public/PPakPatcherLibrary.h（示意）
class PPAKPATCHER_API FPPakPatcher
{
public:
    // 从文件路径打补丁，自动处理读写与分配
    static bool PatchFile(
        const FString& OldFilePath,
        const FString& DiffFilePath,
        const FString& OutNewFilePath,
        int32 ThreadNum = 1,
        FString* OutError = nullptr);

    // 读取补丁元信息（无需真正打补丁）
    static bool GetDiffInfo(
        const FString& DiffFilePath,
        FPPakDiffInfo& OutInfo);
};
```

具体 API 形态请以 `Source/PPakPatcher/Public/` 下的最新头文件为准。

## 4. 内存与线程

- `threadNum` 默认 1。在低端移动端建议保持 1，避免争抢；
- 大文件 patch 时优先使用 stream 形式接口（如有），避免一次性把整个 Pak 读入内存；
- iOS / 部分 Android 设备对单进程内存上限严格，必要时分块或落盘。

## 5. 失败处理

| 失败原因 | 排查方向 |
| -------- | -------- |
| 旧文件哈希不匹配 | 玩家本地 Pak 被改动，需重新走全量下载 |
| 补丁损坏 | CDN 下载校验、断点续传是否正确 |
| 输出大小不等于期望值 | 通过 `GetSingleCompressedDiffInfo` 拿到的 `newDataSize` 与实际文件长度对比 |
| 内存不足 | 改用更小的 `patchStepMemSize` 重新生成补丁 |

## 6. 进一步阅读

- 构建机如何生成补丁：[`commandlet-usage.md`](./commandlet-usage.md)
- 底层 API 参考：[`api-reference.md`](./api-reference.md)
