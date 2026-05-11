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

