# API 参考：build_libs/include/HDiffPatch.h

本文件列出由 `build_libs/include/HDiffPatch.h` 暴露给 UE 项目的全部 C++ API。
所有接口位于命名空间 `HDiffPatch`，均使用 `HDIFFPATCH_API` 宏修饰以正确处理跨平台导入/导出。

## 1. 类型与枚举

### `HDiffCompressionType`

```cpp
enum HDiffCompressionType {
    HDIFF_COMPRESSION_NONE = 0,
    HDIFF_COMPRESSION_ZLIB,
    HDIFF_COMPRESSION_LZMA,
    HDIFF_COMPRESSION_LZMA2,
    HDIFF_COMPRESSION_ZSTD,
    HDIFF_COMPRESSION_LDEF,
    HDIFF_COMPRESSION_BZ2,
};
```

可用算法取决于库构建时启用了哪些 `HDIFFPATCH_USE_*` 选项。

### `SingleCompressedDiffInfo`

补丁的元信息（解析自补丁 header），用于 patch 前预分配 / 决策。

```cpp
struct SingleCompressedDiffInfo {
    unsigned long long newDataSize;       // 新数据大小
    unsigned long long oldDataSize;       // 旧数据大小
    unsigned long long uncompressedSize;  // 未压缩 diff 数据大小（不含 header）
    unsigned long long compressedSize;    // 压缩 diff 数据大小（不含 header），0 表示未压缩
    unsigned long long diffDataPos;       // diff 数据在流中的偏移（header 之后）
    unsigned long long coverCount;        // cover (copy/match) 块数
    unsigned long long stepMemSize;       // 推荐单步 patch 处理的内存大小
    char               compressType[260]; // 压缩算法名（如 "zlib"、"lzma"）；空表示未压缩
};
```

## 2. 创建补丁（仅 `HDIFFPATCH_ENABLE_DIFF=1`）

> Runtime（玩家设备）一般 **不需要** 创建补丁。当前默认 `HDIFFPATCH_ENABLE_DIFF` 始终为 1，
> 但插件实际打包到客户端时，可视情况通过宏裁剪只保留 patch 部分以减少包体。

### `CreateDiff`

经典格式 diff，未压缩。

```cpp
void CreateDiff(
    const unsigned char* newData, const unsigned char* newData_end,
    const unsigned char* oldData, const unsigned char* oldData_end,
    std::vector<unsigned char>& out_diff,
    int    kMinSingleMatchScore = 6,   // bin: 0~4   text: 4~9
    bool   isUseBigCacheMatch   = false,
    size_t threadNum            = 1);
```

### `CheckDiff`

验证 `Patch(oldData, diff) == newData`。

```cpp
bool CheckDiff(
    const unsigned char* newData, const unsigned char* newData_end,
    const unsigned char* oldData, const unsigned char* oldData_end,
    const unsigned char* diff,    const unsigned char* diff_end);
```

### `CreateSingleCompressedDiff`（推荐）

单流压缩 diff，体积更小，patch 可流式。

```cpp
void CreateSingleCompressedDiff(
    const unsigned char* newData, const unsigned char* newData_end,
    const unsigned char* oldData, const unsigned char* oldData_end,
    std::vector<unsigned char>& out_diff,
    HDiffCompressionType compressType         = HDIFF_COMPRESSION_NONE,
    int                  kMinSingleMatchScore = 6,
    size_t               patchStepMemSize     = 256 * 1024,
    bool                 isUseBigCacheMatch   = false,
    size_t               threadNum            = 1);
```

参数建议：

- `kMinSingleMatchScore`：二进制 0~4，文本 4~9。
- `patchStepMemSize`：默认 256K，推荐 64K ~ 2M；越大 patch 越快、内存越多。
- `isUseBigCacheMatch=true` 时 match 速度更快，但建立 cache 慢，且峰值内存约 O(oldSize)。

### `CheckSingleCompressedDiff`

```cpp
bool CheckSingleCompressedDiff(
    const unsigned char* newData, const unsigned char* newData_end,
    const unsigned char* oldData, const unsigned char* oldData_end,
    const unsigned char* diff,    const unsigned char* diff_end,
    size_t threadNum = 1);
```

## 3. 读取补丁信息

### `GetSingleCompressedDiffInfo`

```cpp
bool GetSingleCompressedDiffInfo(
    const unsigned char* diff, const unsigned char* diff_end,
    SingleCompressedDiffInfo* out_info);
```

只读取 header；常用于在 patch 前显示进度、预分配 buffer。

## 4. 应用补丁

### `Patch`

针对 `CreateDiff` 产出的经典格式。

```cpp
bool Patch(
    /*out*/ unsigned char* out_newData, unsigned char* out_newData_end,
    const   unsigned char* oldData,     const unsigned char* oldData_end,
    const   unsigned char* serializedDiff,
    const   unsigned char* serializedDiff_end);
```

### `PatchSingleStream`（推荐）

针对 `CreateSingleCompressedDiff` 产出的单流压缩格式。

```cpp
bool PatchSingleStream(
    /*out*/ unsigned char* out_newData, unsigned char* out_newData_end,
    const   unsigned char* oldData,     const unsigned char* oldData_end,
    const   unsigned char* diff,        const unsigned char* diff_end,
    size_t threadNum = 1);
```

### `PatchSingleCompressedDiff`

接受外部提前解析好的 `SingleCompressedDiffInfo*`，避免重复解析 header。

```cpp
bool PatchSingleCompressedDiff(
    /*out*/ unsigned char* out_newData, unsigned char* out_newData_end,
    const   unsigned char* oldData,     const unsigned char* oldData_end,
    const   unsigned char* diff,        const unsigned char* diff_end,
    const   SingleCompressedDiffInfo* info,
    size_t threadNum = 1);
```

## 5. 调用约束

- 所有指针参数都是 **半开区间** `[begin, end)`。
- `out_newData_end - out_newData` 必须等于 `info.newDataSize`，否则 patch 失败。
- `compressType` 必须是构建库时启用了的算法；否则 `Create*` 会 fallback / 失败、`Patch*` 会失败。
- 单线程 / 多线程：在 `CMakeLists.txt` 的 `HDIFFPATCH_MT=ON` 时 `threadNum>1` 才有意义；
  关闭多线程时所有调用退化为单线程。

## 6. 新增 API 流程

参见 [`build-libs.md` §7](./build-libs.md#7-新增对外-api-的步骤)。
