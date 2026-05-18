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
	LZMA,
	LZMA2,
	ZSTD,
	LDEF,
	BZ2,
};

UENUM()
enum class EPPakPatchMode : uint8
{
	PakAware,	// 模式 2：解析 pak、按 entry 产出补丁（默认）
	Binary,		// 模式 1：对任意二进制资产直接走 HDiff
};

UENUM()
enum class EPPakCheckFileHashType : uint8
{
	None,	// 不校验
	Crc32,	// 校验crc32
	MD5,	// 校验md5
};

/**
 * PakAware 模式下，对单个 entry 字节流的预处理策略。
 * 解密与解压有先后顺序（pak 内字节布局：压缩字节 → 加密 → 写入磁盘；读取时先解密再解压）。
 *
 *   NoDecrypt              : 直接对原始磁盘字节（密文 + 压缩字节）做 HDiff；不解密、不解压。
 *   DecryptAndCompress     : 仅解密（拿到压缩字节）后做 HDiff；不解压。
 *   DecryptAndDecompress   : 解密 + 解压（拿到原始字节）后做 HDiff。
 *
 * 注：取名 DecryptAndCompress / DecryptAndDecompress 是从"diff 输入处于哪个状态"角度命名的，
 *      与"是否做了某动作"对应——即 patch 数据所处的层级。
 */
UENUM()
enum class EPPakAwarePreprocess : uint8
{
	NoDecrypt              = 0,
	DecryptAndCompress     = 1,
	DecryptAndDecompress   = 2,
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

