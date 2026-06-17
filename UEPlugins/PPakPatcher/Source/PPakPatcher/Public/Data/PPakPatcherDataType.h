#pragma once
#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "PPakPatcherDataType.generated.h"

#define DEFAULT_MIN_SINGLE_MATCH_SCORE 6 // default 6, bin: 0--4  text: 4--9
#define DEFAULT_THREAD_NUM 0
#define ENABLE_SINGLE_COMPRESS 1

DECLARE_LOG_CATEGORY_EXTERN(LogPPakPacher, Log, All);

UENUM()
enum class EPPatchDataSourceType : uint8
{
	None,
	Load,
	Record,
};

UENUM()
enum class EPakFilePatchType : uint8
{
	Keep,
	Modify,
	New,
	Delete,
};

UENUM()
enum class EPFileCompareDiffType : uint8
{
	None, // both not exist.
	Equal,
	Add,
	Delete,
	Modify,
};

/**
 * Pak Patch Compress Type ：注意，这里要和HDiffPatch的HDiffCompressionType保持一致
 */
UENUM()
enum class EPakPatchCompressType : uint8
{
	None = 0,
	ZLIB,
	LZMA,	// 不稳定，内部崩溃
	LZMA2,	// 不稳定，内部崩溃
	ZSTD,
	LDEF,	// 不支持
	BZ2,	// 不支持
};

/**
 * Pak patch 总体模式：决定是否解析 pak 结构，以及 PakAware 模式下的 entry 字节流预处理层级。
 *
 * 命名规则：Binary / PakAware<Preprocess>，Preprocess 见下方说明。
 *
 *   Binary                       : 不解析 pak，整文件直接走 HDiff（适用任意二进制资产）
 *   PakAwareNoDecrypt            : 解析 pak、按 entry 走 HDiff；不解密（直接对密文+压缩字节做 diff）
 *   PakAwareDecryptAndCompress   : 解析 pak、按 entry 走 HDiff；解密后保持压缩态做 diff（**默认**）
 *   PakAwareDecryptAndDecompress : 解析 pak、按 entry 走 HDiff；解密+解压后在原始字节做 diff
 *                                  （已禁用，DDC 入口拦截）
 *
 * pak 内字节布局：原始字节 → 压缩 → 加密 → 写入磁盘；读取需先解密再解压。
 * 命名以 "diff 输入处于哪个层级" 为视角（DecryptAndCompress = 已解密、仍处于压缩字节状态）。
 */
UENUM()
enum class EPPakPatchMode : uint8
{
	Binary                       = 0,
	PakAwareNoDecrypt            = 1,
	PakAwareDecryptAndCompress   = 2,	// 推荐 / 默认
	PakAwareDecryptAndDecompress = 3,	// 已禁用（入口拦截）
};

namespace PPakPatchModeHelper
{
	FORCEINLINE bool IsPakAware(EPPakPatchMode M)     { return M != EPPakPatchMode::Binary; }
	FORCEINLINE bool ShouldDecrypt(EPPakPatchMode M)  { return M == EPPakPatchMode::PakAwareDecryptAndCompress || M == EPPakPatchMode::PakAwareDecryptAndDecompress; }
	FORCEINLINE bool ShouldDecompress(EPPakPatchMode M) { return M == EPPakPatchMode::PakAwareDecryptAndDecompress; }
}

/**
 * 外部整体压缩类型：对 .patch 文件的整个 DataBlock（所有 entry diff 数据拼接后）
 * 做一次性压缩，作为 HDiff 内部 EPakPatchCompressType 的替代/补充方案。
 *
 * 优势（相比 HDiff 内部 per-entry 压缩）：
 *   1) 压缩字典/上下文跨越所有 entry diff，能利用 entry 间冗余 → 压缩率更高
 *   2) 使用 UE 引擎自带 Oodle，工业级稳定（绕开 HDiff SDK 的 LZMA/LZMA2 access violation）
 *   3) Oodle Mermaid 解压速度极快，运行时收益明显
 *
 * 推荐使用 Oodle_Mermaid + Optimal4 (level=8)：质量与速度的最佳平衡。
 */
UENUM()
enum class EPPatchExternalCompressType : uint8
{
	None              = 0,
	Oodle_Selkie      = 1,
	Oodle_Mermaid     = 2,	// 推荐：质量/速度均衡
	Oodle_Kraken      = 3,	// 更高压缩率，速度稍慢
	Oodle_Leviathan   = 4,	// 最高压缩率，速度最慢
};

UENUM()
enum class EPPakCheckFileHashType : uint8
{
	None,	// 不校验
	Crc32,	// 校验crc32
	MD5,	// 校验md5
};

class FPFileCompareInfo
{
public:
	FString Filename;
	FString NewFullPath;
	FString OldFullPath;
	int64 OldSize = 0;
	int64 NewSize = 0;
	int32 OldCrc = 0;
	int32 NewCrc = 0;
	FString OldMd5;
	FString NewMd5;
	EPFileCompareDiffType DiffType = EPFileCompareDiffType::None;


	FPFileCompareInfo() {}
};

#define PAKPATCHER_DECLEAR_ENUM_TO_STRING(EnumType) static FString ToString(EnumType InEnumValue)
class PPAKPATCHER_API FPPakPatcherEnumHelper
{
public:
	PAKPATCHER_DECLEAR_ENUM_TO_STRING(EPPatchDataSourceType);
	PAKPATCHER_DECLEAR_ENUM_TO_STRING(EPakFilePatchType);
};
#undef PAKPATCHER_DECLEAR_ENUM_TO_STRING

UCLASS()
class UPPakPatcherDataType : public UObject
{
	GENERATED_UCLASS_BODY()
};

